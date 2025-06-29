//
//  file: %n-control.c
//  summary: "native functions for control flow"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Control constructs follow these rules:
//
// * If they do not run any branches, they return NULL.  This will signal
//   functions like ELSE and THEN.
//
//   (The exception is WHEN, which returns VOID)
//
// * If a branch *does* run--and its evaluation *happens* to produce NULL--the
//   null will be wrapped in a BLOCK! antiform ("heavy null").  This way THEN
//   runs instead of ELSE.  Although this does mean there is some conflation of
//   the results, the conflated values have properties that mostly align with
//   what their intent was--so it works about as well as it can.
//
// * Zero-arity function values used as branches will be executed, and
//   single-arity functions used as branches will also be executed--but passed
//   the value of the triggering condition.  Useful with arrow functions:
//
//       >> if 1 < 2 [10 + 20] then x -> [print ["THEN got" x]]
//       THEN got 30
//
//   (See Eval_Branch_Throws() for supported ANY-BRANCH? types and behaviors.)
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// A. Evaluations must be performed through continuations, so things like IF
//    can't be on the C function stack while the branch runs.  Rather than
//    asking to be called back after the evaluation so it can turn null into
//    a "heavy" pack forms, it requests "branch semantics" so the evaluator
//    does that automatically.  DELEGATE means it doesn't need a callback.
//

#include "sys-core.h"


//
//  The_Group_Branch_Executor: C
//
// Branching code typically uses "soft" literal slots for the branch.  That
// means that if you use a GROUP! there, the parameter gathering process will
// pre-evaluate it.
//
//     >> branchy: func [flag] [either flag '[<a>] '[<b>]]
//
//     >> either okay (print "a" branchy true) (print "b" branchy false)
//     a
//     b
//     == <a>
//
// That behavior might seem less useful than only evaluating the groups in
// the event of the branch running.  And it might seem you could accomplish
// that by taking the arguments as non-soft literal and then having the
// branch handler evaluate the groups when the branch was taken.  However,
// the use of the soft literal convention is important for another reason:
// it's how lambdas are allowed to be implicitly grouped:
//
//     case [...] then x -> [...]
//     =>
//     case [...] then (x -> [...])
//
// This implicit grouping only happens with soft literals.  If THEN does a
// hard literal argument forward, and -> does a hard literal backward, then
// that is a deadlock and there is an error.
//
// So to accomplish the desire of a group that only evaluates to produce the
// branch to run if the branch is to be taken, we use @GROUP!:
//
//     >> either okay @(print "a" branchy true) @(print "b" branchy false)
//     a
//     == <a>
//
// It's not super common to need this.  But if someone does...the way it is
// accomplished is that @GROUP! branches have their own executor.  This
// means something like IF can push a level with the branch executor that
// can complete and run the evaluated-to branch.
//
// So the group branch executor is pushed with the feed of the GROUP! to run.
// It gives this feed to an Stepper_Executor(), and then delegates to the
// branch returned as a result.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. The `with` parameter in continuations isn't required to be GC safe or
//    even distinct from the output cell (see CONTINUE_CORE()).  So whoever
//    dispatched to the group branch executor could have passed a fleeting
//    value pointer...hence it needs to be stored somewhere.  So the group
//    executor expects it to be preloaded into SPARE, or be unreadable.
//
Bounce The_Group_Branch_Executor(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    if (THROWING)
        return THROWN;

    Need(Value*) with = Known_Stable(SPARE);  // passed to branch if run [1]
    Atom* branch = SCRATCH;  // GC-safe if eval target

    enum {
        ST_GROUP_BRANCH_INITIAL_ENTRY = STATE_0,
        ST_GROUP_BRANCH_RUNNING_GROUP,
        ST_GROUP_BRANCH_RUNNING_BRANCH  // no DELEGATE() for executors
    };

    switch (STATE) {
      case ST_GROUP_BRANCH_INITIAL_ENTRY:
        goto initial_entry;

      case ST_GROUP_BRANCH_RUNNING_GROUP:
        goto group_result_in_branch;

      case ST_GROUP_BRANCH_RUNNING_BRANCH:
        return OUT;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. The Trampoline has some sanity checking asserts that try to stop you
    //    from making mistakes.  Because this does something weird to use the
    //    OUT cell as `with` the LEVEL_FLAG_FORCE_HEAVY_NULLS was taken off at
    //    the callsite.
    //
    // 2. For as long as the evaluator is running, its out cell is GC-safe.

    if (Is_Cell_Erased(with))
        Init_Nulled(with);

    Level* sub = Make_Level(
        &Evaluator_Executor,
        LEVEL->feed,
        LEVEL->flags.bits & (~ FLAG_STATE_BYTE(255))  // take out state 1
            & (~ LEVEL_FLAG_FORCE_HEAVY_NULLS)  // take off branch flag [1]
    );
    Init_Void(Evaluator_Primed_Cell(sub));
    Push_Level_Erase_Out_If_State_0(branch, sub);  // branch GC-protected [2]

    STATE = ST_GROUP_BRANCH_RUNNING_GROUP;
    return CONTINUE_SUBLEVEL(sub);

} group_result_in_branch: {  /////////////////////////////////////////////////

    // 1. Allowing a void branch can be useful, consider:
    //
    //        /switch-d: enclose (augment switch/ [
    //            :default "Default case if no others are found"
    //            [block!]
    //        ]) lambda [f [frame!]] [
    //            eval f else (opt f.default)
    //        ]

    assert(Is_Level_At_End(L));

    if (Is_Void(branch))  // void branches giving their input is useful  [1]
        return Copy_Cell(OUT, with);

    Decay_If_Unstable(branch);

    if (Is_Pinned_Form_Of(WORD, branch))
        panic (Error_Bad_Branch_Type_Raw());  // stop recursions (good?)

    STATE = ST_GROUP_BRANCH_RUNNING_BRANCH;
    return CONTINUE(OUT, cast(Value*, branch), with);
}}


//
//  if: native [
//
//  "If CONDITION is not NULL, execute branch, otherwise return NULL"
//
//      return: "will be a PACK! containing NULL if branch evaluates to NULL"
//          [any-atom?]
//      condition [any-value?]
//      @(branch) "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch?]
//  ]
//
DECLARE_NATIVE(IF)
{
    INCLUDE_PARAMS_OF_IF;

    Value* condition = ARG(CONDITION);
    Value* branch = ARG(BRANCH);

    bool cond;
    Option(Error*) e = Trap_Test_Conditional(&cond, condition);
    if (e)
        panic (unwrap e);

    if (not cond)
        return nullptr;  // "light" null (not in a pack) if condition is false

    return DELEGATE_BRANCH(OUT, branch, condition);  // branch semantics [A]
}


//
//  when: native [
//
//  "When CONDITION is not NULL, execute branch, otherwise return VOID"
//
//      return: "will be a PACK! containing NULL if branch evaluates to NULL"
//          [any-atom?]
//      condition [any-value?]
//      @(branch) "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch?]
//  ]
//
DECLARE_NATIVE(WHEN)
{
    INCLUDE_PARAMS_OF_WHEN;

    Value* condition = ARG(CONDITION);
    Value* branch = ARG(BRANCH);

    bool cond;
    Option(Error*) e = Trap_Test_Conditional(&cond, condition);
    if (e)
        panic (unwrap e);

    if (not cond)
        return VOID;  // deviation from IF (!)

    return DELEGATE_BRANCH(OUT, branch, condition);  // branch semantics [A]
}


//
//  either: native [
//
//  "Choose a branch to execute, based on whether CONDITION is NULL"
//
//      return: "will be a PACK! containing NULL if branch evaluates to NULL"
//          [any-atom?]
//      condition [any-value?]
//      @(okay-branch) "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch?]
//      @(null-branch)
//          [any-branch?]
//  ]
//
DECLARE_NATIVE(EITHER)
{
    INCLUDE_PARAMS_OF_EITHER;

    Value* condition = ARG(CONDITION);

    bool cond;
    Option(Error*) e = Trap_Test_Conditional(&cond, condition);
    if (e)
        panic (unwrap e);

    Value* branch = cond ? ARG(OKAY_BRANCH) : ARG(NULL_BRANCH);

    return DELEGATE_BRANCH(OUT, branch, condition);  // branch semantics [A]
}


//
//  then?: native [
//
//  "Test for NOT being a 'light' null (IF THEN? is prefix THEN)"
//
//      return: [logic?]
//      ^atom [any-atom?]
//  ]
//
DECLARE_NATIVE(THEN_Q)
{
    INCLUDE_PARAMS_OF_THEN_Q;

    Atom* atom = Atom_ARG(ATOM);
    return LOGIC(not Is_Light_Null(atom));
}


//
//  else?: native [
//
//  "Test for being a 'light' null (`IF ELSE?` is prefix `ELSE`)"
//
//      return: [logic?]
//      ^atom [any-atom?]
//  ]
//
DECLARE_NATIVE(ELSE_Q)
{
    INCLUDE_PARAMS_OF_ELSE_Q;

    Atom* atom = Atom_ARG(ATOM);
    return LOGIC(Is_Light_Null(atom));
}


//
//  then: infix:defer native [
//
//  "If input is null, return null, otherwise evaluate the branch"
//
//      return: "null if input is null, or branch result"
//          [any-atom?]
//      ^atom "<deferred argument> Run branch if this is not null"
//          [any-atom?]
//      @(branch) "If arity-1 ACTION!, receives value that triggered branch"
//          [<unrun> any-branch?]
//  ]
//
DECLARE_NATIVE(THEN)
{
    INCLUDE_PARAMS_OF_THEN;

    Atom* atom = Atom_ARG(ATOM);
    Value* branch = ARG(BRANCH);

    if (Is_Light_Null(atom))
        return nullptr;

    return DELEGATE_BRANCH(OUT, branch, atom);
}


//
//  else: infix:defer native [
//
//  "If input is not null, return that value, otherwise evaluate the branch"
//
//      return: "Input value if not null, or branch result"
//          [any-atom?]
//      ^atom "<deferred argument> Run branch if this is null"
//          [any-atom?]
//      @(branch) [<unrun> any-branch?]
//  ]
//
DECLARE_NATIVE(ELSE)
{
    INCLUDE_PARAMS_OF_ELSE;

    Atom* atom = Atom_ARG(ATOM);
    Value* branch = ARG(BRANCH);

    if (not Is_Light_Null(atom))
        return COPY(atom);

    return DELEGATE_BRANCH(OUT, branch, atom);
}


//
//  also: infix:defer native [
//
//  "For non-null input, evaluate and discard branch (like a pass-thru THEN)"
//
//      return: "The same value as input, regardless of if branch runs"
//          [any-atom?]
//      ^atom "<deferred argument> Run branch if this is not null"
//          [any-atom?]
//      @(branch) "If arity-1 ACTION!, receives value that triggered branch"
//          [<unrun> any-branch?]
//  ]
//
DECLARE_NATIVE(ALSO)
{
    INCLUDE_PARAMS_OF_ALSO;  // `then func [x] [(...) :x]` => `also [...]`

    Atom* atom = Atom_ARG(ATOM);
    Value* branch = ARG(BRANCH);

    enum {
        ST_ALSO_INITIAL_ENTRY = STATE_0,
        ST_ALSO_RUNNING_BRANCH
    };

    switch (STATE) {
      case ST_ALSO_INITIAL_ENTRY:
        goto initial_entry;

      case ST_ALSO_RUNNING_BRANCH:
        goto discard_branch_result_in_out_and_return_input;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Light_Null(atom))
        return nullptr;

    STATE = ST_ALSO_RUNNING_BRANCH;
    return CONTINUE_BRANCH(OUT, branch, atom);

} discard_branch_result_in_out_and_return_input: {  //////////////////////////

    dont(UNUSED(OUT));  // would corrupt the OUT pointer itself

    return COPY(atom);
}}


#define LEVEL_FLAG_SAW_NON_VOID_OR_NON_GHOST  LEVEL_FLAG_MISCELLANEOUS


typedef enum {
    NATIVE_IS_ANY,
    NATIVE_IS_ALL,
    NATIVE_IS_NONE
} WhichAnyAllNone;


// ANY and ALL were very similar, and it made sense to factor their common
// code out into a single function.
//
Bounce Any_All_None_Native_Core(Level* level_, WhichAnyAllNone which)
{
    INCLUDE_PARAMS_OF_ALL;

    Element* block = Element_ARG(BLOCK);
    Value* predicate = ARG(PREDICATE);

    Value* condition;  // will be found in OUT or SCRATCH

    enum {
        ST_ANY_ALL_NONE_INITIAL_ENTRY = STATE_0,
        ST_ANY_ALL_NONE_EVAL_STEP,
        ST_ANY_ALL_NONE_PREDICATE
    };

    switch (STATE) {
      case ST_ANY_ALL_NONE_INITIAL_ENTRY: goto initial_entry;
      case ST_ANY_ALL_NONE_EVAL_STEP: goto eval_step_dual_in_spare;
      case ST_ANY_ALL_NONE_PREDICATE: goto predicate_result_in_scratch;
      default: assert(false);
    }

  initial_entry: { ///////////////////////////////////////////////////////////

    assert(Not_Level_Flag(LEVEL, SAW_NON_VOID_OR_NON_GHOST));

    Executor* executor;
    if (Is_Pinned_Form_Of(BLOCK, block))
        executor = &Inert_Stepper_Executor;
    else {
        assert(Is_Block(block));
        executor = &Stepper_Executor;
    }

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;
    Level* sub = Make_Level_At(executor, block, flags);
    Push_Level_Erase_Out_If_State_0(SPARE, sub);

    STATE = ST_ANY_ALL_NONE_EVAL_STEP;
    return CONTINUE_SUBLEVEL(sub);

} eval_step_dual_in_spare: {  ////////////////////////////////////////////////

    if (Is_Endlike_Unset(SPARE))
        goto reached_end;

    if (Is_Ghost_Or_Void(SPARE)) {  // no vote...ignore and continue
        assert(STATE == ST_ANY_ALL_NONE_EVAL_STEP);
        Reset_Evaluator_Erase_Out(SUBLEVEL);
        return CONTINUE_SUBLEVEL(SUBLEVEL);
    }

    Set_Level_Flag(LEVEL, SAW_NON_VOID_OR_NON_GHOST);

    Value* spare = Decay_If_Unstable(SPARE);

    if (not Is_Nulled(predicate))
        goto run_predicate_on_eval_product;

    condition = spare;  // w/o predicate, `condition` is eval result
    goto process_condition;

} run_predicate_on_eval_product: {  //////////////////////////////////////////

    // 1. The predicate-running is pushed over the "keepalive" stepper, but we
    //    don't want the stepper to take a step before coming back to us.
    //    Temporarily patch out the Stepper_Executor() so we get control
    //    back without that intermediate step.

    SUBLEVEL->executor = &Just_Use_Out_Executor;  // tunnel thru [1]

    STATE = ST_ANY_ALL_NONE_PREDICATE;
    return CONTINUE(SCRATCH, predicate, SPARE);

} predicate_result_in_scratch: {  ////////////////////////////////////////////

    // 1. The only way a falsey evaluation should make it to the end is if a
    //    predicate let it pass.  Don't want that to trip up `if all` so make
    //    it heavy...but this way `(all:predicate [null] not?/) then [<runs>]`

    if (Is_Void(SCRATCH))  // !!! should void predicate results opt-out?
        panic (Error_Bad_Void());

    if (Is_Light_Null(SCRATCH))  // predicates can approve null [1]
        Init_Heavy_Null(SCRATCH);

    SUBLEVEL->executor = &Stepper_Executor;  // done tunneling [2]
    STATE = ST_ANY_ALL_NONE_EVAL_STEP;

    condition = Decay_If_Unstable(SCRATCH);

} process_condition: {

    bool cond;
    Option(Error*) e = Trap_Test_Conditional(&cond, condition);
    if (e)
        panic (unwrap e);

    switch (which) {
      case NATIVE_IS_ANY:
        if (cond)
            goto return_spare;  // successful ANY clause returns the value
        break;

      case NATIVE_IS_ALL:
        if (not cond)
            goto return_null;  // failed ALL clause returns null
        Move_Atom(OUT, SPARE);  // leaves SPARE as fresh...good for next step
        break;

      case NATIVE_IS_NONE:
        if (cond)
            goto return_null;  // succeeding NONE clause returns null
        break;
    }

    if (Try_Is_Level_At_End_Optimization(SUBLEVEL))
        goto reached_end;

    assert(STATE == ST_ANY_ALL_NONE_EVAL_STEP);
    Reset_Evaluator_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} reached_end: {  ////////////////////////////////////////////////////////////

    // 1. Historically there has been controversy over what should be done
    //    about (all []) and (any []).  Languages with variadic short-circuit
    //    AND + OR operations typically say empty AND-ing is truthy, while
    //    empty OR-ing is falsey.
    //
    //    There are reasonable intuitive arguments for that--*if* those are
    //    your only two choices.  Because Ren-C has the option of VOID, it's
    //    better to signal to the caller that nothing happened.  Other choices
    //    can be forced with e.g. (all [... null]) or (any [... okay])

    Drop_Level(SUBLEVEL);

    if (Not_Level_Flag(LEVEL, SAW_NON_VOID_OR_NON_GHOST))
        return VOID;  // return void if all evaluations vaporized [1]

    switch (which) {
      case NATIVE_IS_ANY:
        return nullptr;  // non-vanishing expressions, but none of them passed

      case NATIVE_IS_ALL:
        return BRANCHED(OUT);  // successful ALL returns the last value

      case NATIVE_IS_NONE:
        return OKAY;  // successful NONE has no value to return, use OKAY

      default:  // some C compilers don't seem to know this is unreachable
        crash (nullptr);
    }

} return_spare: { ////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    Move_Atom(OUT, SPARE);
    return BRANCHED(OUT);

} return_null: { /////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    return nullptr;
}}


//
//  all: native [
//
//  "Short-circuiting variant of AND, using a block of expressions as input"
//
//      return: "Product of last passing evaluation if all truthy, else null"
//          [any-value?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! @block!]
//      :predicate "Test for whether an evaluation passes (default is DID)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(ALL)
{
    return Any_All_None_Native_Core(LEVEL, NATIVE_IS_ALL);
}


//
//  any: native [
//
//  "Short-circuiting version of OR, using a block of expressions as input"
//
//      return: "First passing evaluative result, or null if none pass"
//          [any-value?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! @block!]
//      :predicate "Test for whether an evaluation passes (default is DID)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(ANY)
{
    return Any_All_None_Native_Core(LEVEL, NATIVE_IS_ANY);
}


//
//  none: native [
//
//  "Short-circuiting shorthand for NOT ALL"
//
//      return: "First passing evaluative result, or null if none pass"
//          [any-value?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! @block!]
//      :predicate "Test for whether an evaluation passes (default is DID)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(NONE)
{
    return Any_All_None_Native_Core(LEVEL, NATIVE_IS_NONE);
}


//
//  case: native [
//
//  "Evaluates each condition, and when true, evaluates what follows it"
//
//      return: "Last matched case evaluation, or null if no cases matched"
//          [any-value?]
//      cases "Conditions followed by branches"
//          [block!]
//      :all "Do not stop after finding first logically true case"
//      :predicate "Unary case-processing action (default is DID)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(CASE)
{
    INCLUDE_PARAMS_OF_CASE;

    Element* cases = Element_ARG(CASES);
    Value* predicate = ARG(PREDICATE);

    enum {
        ST_CASE_INITIAL_ENTRY = STATE_0,
        ST_CASE_CONDITION_EVAL_STEP,
        ST_CASE_RUNNING_PREDICATE,
        ST_CASE_EVALUATING_GROUP_BRANCH,
        ST_CASE_RUNNING_BRANCH
    };

    switch (STATE) {
      case ST_CASE_INITIAL_ENTRY: goto initial_entry;
      case ST_CASE_CONDITION_EVAL_STEP: goto condition_dual_in_spare;
      case ST_CASE_RUNNING_PREDICATE: goto predicate_result_in_spare;
      case ST_CASE_EVALUATING_GROUP_BRANCH:
        goto handle_processed_branch_in_scratch;
      case ST_CASE_RUNNING_BRANCH: goto branch_result_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Level* L = Make_Level_At(
        &Stepper_Executor,
        cases,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    );

    Push_Level_Erase_Out_If_State_0(SPARE, L);

    assert(Is_Cell_Erased(OUT));  // erased if STATE_0
    assert(Is_Cell_Erased(SPARE));  // erased if STATE_0

} handle_next_clause: {  /////////////////////////////////////////////////////

    // 1. It may seem tempting to run PREDICATE from on `f` directly, allowing
    //    it to take arity > 2.  But we have to get a true/false answer
    //    *and* know what the right hand argument was, for fallout to work.

    Erase_Cell(SPARE);  // must do before goto reached_end

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;

    STATE = ST_CASE_CONDITION_EVAL_STEP;
    SUBLEVEL->executor = &Stepper_Executor;  // undo &Just_Use_Out_Executor
    Reset_Evaluator_Erase_Out(SUBLEVEL);

    return CONTINUE_SUBLEVEL(SUBLEVEL);  // one step to pass predicate [1]

} condition_dual_in_spare: {  ////////////////////////////////////////////////

    if (Is_Ghost(SPARE))  // skip over things like ELIDE, but not voids!
        goto handle_next_clause;

    Decay_If_Unstable(SPARE);

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;  // we tolerate "fallout" from a condition

    if (Is_Nulled(predicate))
        goto processed_result_in_spare;

    STATE = ST_CASE_RUNNING_PREDICATE;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    return CONTINUE(SPARE, predicate, SPARE);  // with == out is legal

} predicate_result_in_spare: {  //////////////////////////////////////////////

    // 1. Expressions between branches are allowed to vaporize via GHOST
    //    (e.g. ELIDE), but voids are not skipped.
    //
    //        >> condition: null
    //        >> case [opt if condition [<a>] [print "Whoops?"] [<hmm>]]
    //        Whoops?
    //        == <hmm>

    if (Is_Void(SPARE))  // error on void predicate results (not same as [1])
        panic (Error_Bad_Void());

    goto processed_result_in_spare;

} processed_result_in_spare: {  //////////////////////////////////////////////

    // 1. We want this to panic:
    //
    //       >> foo: func [] [return case [okay ["a"]]]
    //
    //       >> append foo "b"
    //       ** Access Error: CONST or iterative value (see MUTABLE): "a"
    //
    //    So the FUNC's const body evaluation led to CASE's argument block
    //    being evaluated as const.  But we have to proxy that const flag
    //    over to the block.

    Element* branch = Derelativize(
        SCRATCH, At_Level(SUBLEVEL), Level_Binding(SUBLEVEL)
    );
    Inherit_Const(branch, cases);  // branch needs to respect const [1]

    Fetch_Next_In_Feed(SUBLEVEL->feed);

    if (not Is_Group(branch))
        goto handle_processed_branch_in_scratch;

    Level* sub = Make_Level_At_Inherit_Const(
        &Evaluator_Executor,
        branch,  // non @GROUP! branches are run unconditionally
        Level_Binding(SUBLEVEL),
        LEVEL_MASK_NONE
    );
    Init_Void(Evaluator_Primed_Cell(sub));

    STATE = ST_CASE_EVALUATING_GROUP_BRANCH;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    Push_Level_Erase_Out_If_State_0(SCRATCH, sub);  // level has array and index
    return CONTINUE_SUBLEVEL(sub);

} handle_processed_branch_in_scratch: {  /////////////////////////////////////

    // 1. Maintain symmetry with IF on non-taken branches:
    //
    //        >> if null <some-tag>
    //        ** Script Error: if does not allow tag! for its branch...

    Value* branch = Decay_If_Unstable(SCRATCH);

    Value* spare = Decay_If_Unstable(SPARE);

    bool cond;
    Option(Error*) e = Trap_Test_Conditional(&cond, spare);
    if (e)
        panic (unwrap e);

    if (not cond) {
        if (not Any_Branch(branch))  // like IF [1]
            panic (Error_Bad_Value_Raw(branch));  // stable

        goto handle_next_clause;
    }

    STATE = ST_CASE_RUNNING_BRANCH;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    return CONTINUE_BRANCH(OUT, branch, SPARE);

} branch_result_in_out: {  ///////////////////////////////////////////////////

    if (not Bool_ARG(ALL)) {
        Drop_Level(SUBLEVEL);
        return BRANCHED(OUT);
    }

    goto handle_next_clause;

} reached_end: {  ////////////////////////////////////////////////////////////

    // 1. Last evaluation will "fall out" if there is no branch:
    //
    //        >> case [null [<a>] null [<b>]]
    //        == ~null~  ; anti
    //
    //        >> case [null [<a>] null [<b>] 10 + 20]
    //        == 30
    //
    //    It's a little bit like a quick-and-dirty ELSE (or /DEFAULT), however
    //    when you use CASE:ALL it's what is returned even if there's a match:
    //
    //        >> case:all [1 < 2 [<a>] 3 < 4 [<b>]]
    //        == <b>
    //
    //        >> case:all [1 < 2 [<a>] 3 < 4 [<b>] 10 + 20]
    //        == 30  ; so not the same as an ELSE, it's just "fallout"
    //
    //    This counts as a "branch taken", so void and null are boxed into an
    //    antiform pack.

    assert(Bool_ARG(ALL) or Is_Cell_Erased(OUT));  // never ran branch, or :ALL

    Drop_Level(SUBLEVEL);

    if (Not_Cell_Erased(SPARE)) {  // prioritize fallout result [1]
        Move_Atom(OUT, SPARE);
        return BRANCHED(OUT);
    }

    if (Is_Cell_Erased(OUT))  // none of the clauses of an :ALL ran a branch
        return Init_Nulled(OUT);

    return BRANCHED(OUT);
}}


//
//  switch: native [
//
//  "Selects a choice and evaluates the block that follows it"
//
//      return: "Last case evaluation, or null if no cases matched"
//          [any-value?]
//      value [any-value?]
//      cases "Block of cases (comparison lists followed by block branches)"
//          [block!]
//      :all "Evaluate all matches (not just first one)"
//      :type "Match based on type constraints, not equality"
//      :predicate "Binary switch-processing action (default is EQUAL?)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(SWITCH)
{
    INCLUDE_PARAMS_OF_SWITCH;

    Value* left = ARG(VALUE);
    Element* cases = Element_ARG(CASES);
    Value* predicate = ARG(PREDICATE);

    enum {
        ST_SWITCH_INITIAL_ENTRY = STATE_0,
        ST_SWITCH_EVALUATING_RIGHT,
        ST_SWITCH_RUNNING_BRANCH
    };

    switch (STATE) {
      case ST_SWITCH_INITIAL_ENTRY:
        goto initial_entry;

      case ST_SWITCH_EVALUATING_RIGHT:
        goto right_dual_in_spare;

      case ST_SWITCH_RUNNING_BRANCH:
        if (not Bool_ARG(ALL)) {
            Drop_Level(SUBLEVEL);
            return BRANCHED(OUT);
        }
        goto next_switch_step;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. Originally this called the "guts" of comparison by default, instead
    //    of invoking the EQUAL? native.  But the comparison guts are no
    //    longer available without a frame.  So really this just needs to
    //    be worked on and sped up, such as to create one frame and reuse
    //    it over and over.  Review.

    assert(Is_Cell_Erased(SPARE));  // initial condition
    assert(Is_Cell_Erased(OUT));  // if no writes to out performed, we act void

    if (Bool_ARG(TYPE) and Bool_ARG(PREDICATE))
        panic (Error_Bad_Refines_Raw());

    if (not Bool_ARG(TYPE) and not Bool_ARG(PREDICATE)) {
        Copy_Cell(predicate, LIB(EQUAL_Q));  // no more builtin comparison [1]
        LIFT_BYTE(predicate) = NOQUOTE_2;
    }

    Level* sub = Make_Level_At(
        &Stepper_Executor,
        cases,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    );

    Push_Level_Erase_Out_If_State_0(SPARE, sub);

} next_switch_step: {  ///////////////////////////////////////////////////////

    // 1. With switch, we have one fixed value ("left") and then an evaluated
    //    value from the block ("right") passed to a comparison predicate to
    //    determine a match.  It may seem tempting to build a frame for the
    //    predicate partially specialized with left, and allow it to consume
    //    the right from the feed...allowing it to take arity > 2 (as well as
    //    to honor any quoting convention the predicate has.
    //
    //    Right now that's not what we do, since it would preclude being able
    //    to have "fallout".  This should probably be reconsidered, but there
    //    are some other SWITCH redesign questions up in the air already:
    //
    //      https://forum.rebol.info/t/match-in-rust-vs-switch/1835

    Erase_Cell(SPARE);  // fallout must be reset each time

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;

    const Element* at = At_Level(SUBLEVEL);

    if (Is_Block(at) or Is_Frame(at)) {  // seen with no match in effect
        Fetch_Next_In_Feed(SUBLEVEL->feed);  // just skip over it
        goto next_switch_step;
    }

    STATE = ST_SWITCH_EVALUATING_RIGHT;
    SUBLEVEL->executor = &Stepper_Executor;
    Reset_Evaluator_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);  // no direct predicate call [1]

} right_dual_in_spare: {  ////////////////////////////////////////////////////

    // 1. At one point the value was allowed to corrupt during comparison, due
    //    to the idea equality was transitive.  So if it changes 0.01 to 1% in
    //    order to compare it, anything 0.01 would have compared equal to so
    //    will 1%.  (Would be required for `a = b` and `b = c` to properly
    //    imply `a = c`.)
    //
    //    HOWEVER this mutated the branch fallout, and quote removals were
    //    distorting comparisons.  So it copies into a scratch location.
    //
    // 2. We want this to panic:
    //
    //       >> foo: func [] [return switch 1 + 2 [3 ["a"]]]
    //
    //       >> append foo "b"
    //       ** Access Error: CONST or iterative value (see MUTABLE): "a"
    //
    //    So the FUNC's const body evaluation led to SWITCH's argument block
    //    being evaluated as const.  But we have to proxy that const flag
    //    over to the block.

    if (Is_Ghost(SPARE))  // skip comments or ELIDEs
        goto next_switch_step;

    if (Is_Void(SPARE))
        panic (Error_Bad_Void());

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;  // nothing left, so drop frame and return

    Value* spare = Decay_If_Unstable(SPARE);  // !!! predicate wants decayed?

    if (Bool_ARG(TYPE)) {
        if (not Is_Datatype(spare) and not Is_Action(spare))
            panic ("switch:type conditions must be DATATYPE! or ACTION!");

        Copy_Cell(Level_Spare(SUBLEVEL), left);  // spare of the *sublevel!*
        if (not Typecheck_Atom_In_Spare_Uses_Scratch(  // *sublevel*'s SPARE!
            SUBLEVEL, spare, SPECIFIED
        )){
            goto next_switch_step;
        }
    }
    else {
        assert(not Is_Nulled(predicate));

        Sink(Value) scratch = SCRATCH;
        if (rebRunThrows(
            scratch,  // <-- output cell
            predicate,
                rebQ(left),  // first arg (left hand side if infix)
                rebQ(spare)  // second arg (right side if infix)
        )){
            return BOUNCE_THROWN;  // aborts sublevel
        }

        bool cond;
        Option(Error*) e = Trap_Test_Conditional(&cond, scratch);
        if (e)
            panic (unwrap e);

        if (not cond)
            goto next_switch_step;
    }

    const Element* at = Try_At_Level(SUBLEVEL);

    while (true) {  // skip ahead for BLOCK!/ACTION! to process the match
        if (at == nullptr)
            goto reached_end;

        if (Is_Block(at) or Is_Meta_Form_Of(BLOCK, at) or Is_Frame(at))
            break;

        Fetch_Next_In_Feed(SUBLEVEL->feed);
        at = At_Level(SUBLEVEL);
    }

    Element* scratch = Derelativize(SCRATCH, at, Level_Binding(SUBLEVEL));
    Inherit_Const(scratch, cases);  // need to inherit proxy const bit [3]

    STATE = ST_SWITCH_RUNNING_BRANCH;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    return CONTINUE_BRANCH(OUT, scratch, spare);

} reached_end: {  ////////////////////////////////////////////////////////////

    // 1. Fallout is used in situations like:
    //
    //        lib: switch config.platform [
    //            'Windows [%windows.lib]
    //            'Linux [%linux.a]
    //            %whatever.a
    //        ]
    //
    //    These cases still count as "branch taken", so if a null or void fall
    //    out they will be put in a pack.  (See additional remarks in CASE)

    assert(Bool_ARG(ALL) or Is_Cell_Erased(OUT));

    Drop_Level(SUBLEVEL);

    if (Not_Cell_Erased(SPARE)) {  // something counts as fallout [1]
        Assert_Cell_Stable(SPARE);
        Move_Atom(OUT, SPARE);
        return BRANCHED(OUT);
    }

    if (Is_Cell_Erased(OUT))  // no fallout, and no branches ran
        return Init_Nulled(OUT);

    return BRANCHED(OUT);
}}


//
//  default: infix native [
//
//  "Set word or path to a calculated value if it is not set"
//
//      return: "Former value or branch result"
//          [any-value?]
//      @target "Word or path which might be set (or not)"
//          [set-group? set-word? set-tuple?]  ; to left of DEFAULT
//      @(branch) "If target needs default, this is evaluated and stored there"
//          [any-branch?]
//  ]
//
DECLARE_NATIVE(DEFAULT)
//
// 1. Right now, the GET and SET mechanisms create a nested Trampoline stack,
//    and do not yield to the already-running Trampoline.  This would limit
//    GETTER and SETTER if it had to do something that would yield (e.g. to
//    the JavaScript event loop), so this should be revisited.
{
    INCLUDE_PARAMS_OF_DEFAULT;

    Element* target = Element_ARG(TARGET);
    Value* branch = ARG(BRANCH);

    enum {
        ST_DEFAULT_INITIAL_ENTRY = STATE_0,
        ST_DEFAULT_GETTING_TARGET,
        ST_DEFAULT_EVALUATING_BRANCH
    };

    switch (STATE) {
      case ST_DEFAULT_INITIAL_ENTRY: goto initial_entry;
      case ST_DEFAULT_GETTING_TARGET: assert(false); break;  // !!! TBD [1]
      case ST_DEFAULT_EVALUATING_BRANCH: goto branch_result_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. TARGET may be something like a TUPLE! that contains GROUP!s.  This
  //    puts us at risk of double-evaluation if we do a GET to check the
  //    variable--find it's unset--and use that tuple again.  GET and SET have
  //    an answer for this problem by giving back a block of "steps" which can
  //    resolve the variable without doing more evaluations.
  //
  // 2. Right now GET allows for ERROR! to be returned in cases like a missing
  //    field from an OBJECT!.  This may not be a good idea, given that ^META
  //    fields can legitimately give back ERROR! in-band if a field stores
  //    a lifted error.  It's under review.
  //
  // 3. TRASH!, BLANK, and NULL are considered "defaultable".  VOID can't be
  //    stored in variables directly, but it might be that metavariables such
  //    as (^x: void, ^x: default [1020]) should be willing to overwrite the
  //    void state.  Review.

    Element* steps = u_cast(Element*, SCRATCH);  // avoid double-eval [1]
    STATE = ST_DEFAULT_GETTING_TARGET;  // can't leave at STATE_0

    Unchain(target);
    heeded(Copy_Cell(SCRATCH, target));
    heeded(Corrupt_Cell_If_Needful(SPARE));

    heeded(Init_Dual_Nulled_Pick_Signal(OUT));

    Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out(level_, steps);
    if (e)
        panic (unwrap e);

    if (Is_Error(OUT))
        panic (Cell_Error(OUT));

    if (not Is_Dual_Word_Unset_Signal(Known_Stable(OUT))) {
        Unliftify_Undecayed(OUT);

        Value* out = Decay_If_Unstable(OUT);  // decay may be needed [2]

        if (not (Is_Trash(out) or Is_Nulled(out) or Is_Blank(out)))
            return OUT;  // consider it a "value" [3]
    }

    STATE = ST_DEFAULT_EVALUATING_BRANCH;
    return CONTINUE(OUT, branch, OUT);

} branch_result_in_out: {  ///////////////////////////////////////////////////

    assert(Is_Pinned(Known_Element(SCRATCH)));  // steps is the "var" to set
    heeded(Corrupt_Cell_If_Needful(SPARE));

    Option(Error*) e = Trap_Set_Var_In_Scratch_To_Out(LEVEL, NO_STEPS);
    if (e) {
        assert(false);  // shouldn't be able to happen (steps is pinned)
        panic (unwrap e);
    }

    return OUT;
}}


//
//  maybe: infix native [
//
//  "If the right hand side is not NULL, overwrite the left hand side"
//
//      return: "Former value or branch result"
//          [any-value?]
//      @target "Word or tuple which might be set (or not)"
//          [set-group? set-word? set-tuple?]  ; should do set-block!, etc [1]
//      ^atom "Quantity used to overwrite the left if not null"
//          [any-atom?]  ; to do set-block! etc. needs to take PACK!
//  ]
//
DECLARE_NATIVE(MAYBE)
//
// 1. At time of writing this doesn't support BLOCK! or ^WORD! on the left
//    hand side.  But it should be able to, so it takes the argument as a meta
//    value of any atom.
{
    INCLUDE_PARAMS_OF_MAYBE;

    Element* target = Element_ARG(TARGET);
    Atom* atom = Atom_ARG(ATOM);

    if (Is_Error(atom))
        return COPY(atom);  // pass through but don't assign anything

    Element* quoted_target = Quotify(target);

    if (Is_Light_Null(atom))
        return rebDelegate("get:any", quoted_target);

    Element* lifted_atom = Liftify(atom);

    return rebDelegate(CANON(SET), quoted_target, lifted_atom);  // may decay
}


//
//  catch*: native [
//
//  "Catches a throw from a block and returns its value"
//
//      return: "Thrown value"
//          [any-value?]
//      name "Name of the THROW construct to use"
//          [word!]
//      block "Block to evaluate"
//          [block!]
//  ]
//
DECLARE_NATIVE(CATCH_P)  // specialized to plain CATCH w/ NAME="THROW" in boot
{
    INCLUDE_PARAMS_OF_CATCH_P;

    Element* block = Element_ARG(BLOCK);
    Element* name = Element_ARG(NAME);
    Level* catch_level = level_;

    enum {
        ST_CATCH_INITIAL_ENTRY = STATE_0,
        ST_CATCH_RUNNING_CODE
    };

    switch (STATE) {
      case ST_CATCH_INITIAL_ENTRY:
        goto initial_entry;

      case ST_CATCH_RUNNING_CODE:
        goto code_result_in_out;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Context* parent = List_Binding(block);
    Let* let_throw = Make_Let_Variable(Word_Symbol(name), parent);

    Init_Action(
        Stub_Cell(let_throw),
        Frame_Phase(LIB(DEFINITIONAL_THROW)),
        Word_Symbol(name),  // relabel (THROW in lib is a dummy action)
        Varlist_Of_Level_Force_Managed(catch_level)  // what to continue
    );

    Tweak_Cell_Binding(block, let_throw);  // extend chain

    STATE = ST_CATCH_RUNNING_CODE;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // not caught by default
    return CONTINUE_BRANCH(OUT, ARG(BLOCK));

} code_result_in_out: {  //////////////////////////////////////////////////////

    if (not THROWING)
        return nullptr;  // no throw means just return null (pure, for ELSE)

    const Value* label = VAL_THROWN_LABEL(LEVEL);
    if (not Any_Context(label))
        return THROWN;  // not a context throw, not from DEFINITIONAL-THROW

    VarList* throw_varlist = Cell_Varlist(label);
    if (throw_varlist != Varlist_Of_Level_Maybe_Unmanaged(catch_level))
        return THROWN;  // context throw, but not to this CATCH*, keep going

    CATCH_THROWN(OUT, level_); // thrown value
    return BRANCHED(OUT);  // a caught NULL triggers THEN, not ELSE
}}


//
//  definitional-throw: native [
//
//  "Throws control back to a previous catch"
//
//      return: [<divergent>]
//      ^atom "What CATCH will receive (unstable antiforms ok, e.g. RAISED?)"
//          [any-atom?]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_THROW)
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_THROW;

    Atom* atom = Atom_ARG(ATOM);

    Level* throw_level = LEVEL;  // Level of this RETURN call

    Option(VarList*) coupling = Level_Coupling(throw_level);
    if (not coupling)
        panic (Error_Archetype_Invoked_Raw());

    Element* label = Init_Frame(
        SCRATCH, cast(ParamList*, unwrap coupling), ANONYMOUS, NONMETHOD
    );
    return Init_Thrown_With_Label(LEVEL, atom, label);
}
