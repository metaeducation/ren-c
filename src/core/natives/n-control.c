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
// * If they do not run any branches, they return GHOST.  This will signal
//   functions like ELSE and THEN.
//
//   (The exception is WHEN, which returns NONE)
//
// * If a branch *does* run--and its evaluation *happens* to produce GHOST,
//   the result will be an empty GROUP! antiform (pack!), a.k.a. "heavy void".
//   This way THEN runs instead of ELSE.  The same is true if it happens to
//   produce a NULL--it's wrapped in a pack to be a "heavy null".
//
//   Although this does mean there is some conflation of the results, the
//   conflated values have properties that mostly align with what their intent
//   was--so it works about as well as it can.
//
// * Zero-arity function values used as branches will be executed, and
//   single-arity functions used as branches will also be executed--but passed
//   the value of the triggering condition.  Useful with arrow functions:
//
//       >> if 1 < 2 [10 + 20] then (x -> [print ["THEN got" x]])
//       THEN got 30
//
//   (See Pushed_Continuation() for supported ANY-BRANCH? types and behaviors.)
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
//  Group_Branch_Executor: C
//
// Branches do not use escapable literals for GROUP! evaluations, they get
// the group literally and only run it if the branch is taken:
//
//     >> branchy: lambda [flag] [either flag '[<a>] '[<b>]]
//
//     >> either okay (print "a" branchy okay) (print "b" branchy null)
//     a
//     == <a>
//
// This executor is used to run the GROUP! and then do the eval of whatever
// branch is produced.
//
// !!! There are opportunities for optimization here.  The Level could be
// morphed directly into an Evaluator_Executor() after the branch gets
// evaluated.  Also, analysis of the GROUP! could handle simple patterns
// like (x -> [x + 1]) by noticing it was a 3 element group, and simply bind
// the block to a variable named X and run it instead of creating a function.
//
Bounce Group_Branch_Executor(Level* const L)
//
// 1. The `with` parameter in continuations isn't required to be GC safe or
//    even distinct from the output cell (see CONTINUE_CORE()).  So whoever
//    dispatched to the group branch executor could have passed a fleeting
//    value pointer...hence it needs to be stored somewhere.  So the group
//    executor expects it to be preloaded into SPARE, or be unreadable.
{
    USE_LEVEL_SHORTHANDS (L);

    if (THROWING)
        return THROWN;

    Value* with = SPARE;  // passed to branch if run [1]
    possibly(Is_Cell_Erased(with));
    Value* branch = SCRATCH;  // GC-safe if eval target

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
  //    OUT cell as `with` the LEVEL_FLAG_FORCE_HEAVY_BRANCH was taken off at
  //    the callsite.
  //
  // 2. For as long as the evaluator is running, its out cell is GC-safe.

    if (Is_Cell_Erased(with))
        Init_Nulled(with);

    require (
      Level* sub = Make_Level(
        &Evaluator_Executor,
        LEVEL->feed,
        (not LEVEL_FLAG_FORCE_HEAVY_BRANCH)
            | (not LEVEL_FLAG_AFRAID_OF_GHOSTS)  // all voids act same here
    ));
    Init_Ghost(Evaluator_Primed_Cell(sub));
    Push_Level_Erase_Out_If_State_0(branch, sub);  // branch GC-protected [2]

    STATE = ST_GROUP_BRANCH_RUNNING_GROUP;
    return CONTINUE_SUBLEVEL(sub);

} group_result_in_branch: {  /////////////////////////////////////////////////

  // 1. Allowing a void branch can be useful, consider:
  //
  //        switch-d: enclose (augment switch/ [
  //            :default "Default case if no others are found"
  //                [block!]
  //        ]) f -> [
  //            eval f else (opt f.default)
  //        ]
  //
  //    If we make this evaluate to what (eval f) would have been if there
  //    was no ELSE clause, that makes SWITCH-D with no default behave like
  //    a plain SWITCH.
  //
  // 2. The `return DELEGATE(...)` pattern is a feature provied by the
  //    Action_Executor().  But since this is its own executor, it the service
  //    isn't available and so we must handle the result callback...even
  //    though all we do is `return OUT;`

    assert(Is_Level_At_End(L));

    if (Any_Void(branch))  // void branches giving their input is useful  [1]
        return Copy_Cell(OUT, with);

    require (
      Decay_If_Unstable(branch)
    );
    if (Is_Pinned_Form_Of(WORD, branch))
        panic (Error_Bad_Branch_Type_Raw());  // stop recursions (good?)

    STATE = ST_GROUP_BRANCH_RUNNING_BRANCH;
    return CONTINUE(OUT, cast(Stable*, branch), with);
}}


//
//  if: native [
//
//  "If CONDITION is not NULL, execute BRANCH, otherwise return GHOST!"
//
//      return: [any-value? ghost!]
//      condition [any-stable?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(IF)
{
    INCLUDE_PARAMS_OF_IF;

    Stable* condition = ARG(CONDITION);
    Stable* branch = ARG(BRANCH);

    require (
      bool cond = Test_Conditional(condition)
    );
    if (not cond)
        return GHOST;  // "light" void (triggers ELSE)

    return DELEGATE_BRANCH(OUT, branch, condition);  // branch semantics [A]
}


//
//  when: native [
//
//  "When CONDITION is not NULL, execute BRANCH, otherwise return NONE"
//
//      return: [any-value? none?]
//      condition [any-stable?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(WHEN)
{
    INCLUDE_PARAMS_OF_WHEN;

    Stable* condition = ARG(CONDITION);
    Stable* branch = ARG(BRANCH);

    require (
      bool cond = Test_Conditional(condition)
    );
    if (not cond)
        return Init_None(OUT);  // empty splice (triggers THEN)

    return DELEGATE_BRANCH(OUT, branch, condition);  // branch semantics [A]
}


//
//  either: native [
//
//  "When CONDITION is NULL, run NULL-BRANCH, else run NON-NULL-BRANCH"
//
//      return: [any-value? heavy-null?]
//      condition [any-stable?]
//      @non-null-branch [any-branch?]
//      @null-branch [any-branch?]
//  ]
//
DECLARE_NATIVE(EITHER)
{
    INCLUDE_PARAMS_OF_EITHER;

    Stable* condition = ARG(CONDITION);

    require (
      bool cond = Test_Conditional(condition)
    );

    Stable* branch = cond ? ARG(NON_NULL_BRANCH) : ARG(NULL_BRANCH);

    return DELEGATE_BRANCH(OUT, branch, condition);  // branch semantics [A]
}


//
//  then?: native [
//
//  "Test for NOT being a 'light' null (IF THEN? is prefix THEN)"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(THEN_Q)
{
    INCLUDE_PARAMS_OF_THEN_Q;

    Value* v = ARG(VALUE);
    return LOGIC(not (Is_Light_Null(v) or Is_Ghost(v)));
}


//
//  else?: native [
//
//  "Test for being a 'light' null (`IF ELSE?` is prefix `ELSE`)"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(ELSE_Q)
{
    INCLUDE_PARAMS_OF_ELSE_Q;

    Value* v = ARG(VALUE);
    return LOGIC(Is_Light_Null(v) or Is_Ghost(v));
}


//
//  then: infix:defer native [  ; NOTE - INFIX:DEFER
//
//  "If LEFT is NULL or GHOST!, return it, otherwise return EVAL BRANCH"
//
//      return: [any-value?]
//      ^left [<null> ghost! any-value?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(THEN)
{
    INCLUDE_PARAMS_OF_THEN;

    Value* left = ARG(LEFT);
    Stable* branch = ARG(BRANCH);

    if (Is_Error(left))
        return COPY(left);

    if (Is_Light_Null(left) or Is_Ghost(left))
        return COPY(left);

    return DELEGATE_BRANCH(OUT, branch, left);
}


//
//  thence: native [
//
//  "If VALUE is NULL or GHOST!, return it, otherwise return EVAL BRANCH"
//
//      return: [any-value?]
//      @branch [any-branch?]
//      ^value [<null> ghost! any-value?]
//  ]
//
DECLARE_NATIVE(THENCE)
{
    INCLUDE_PARAMS_OF_THENCE;

    Stable* branch = ARG(BRANCH);
    Value* v = ARG(VALUE);

    if (Is_Error(v))
        return COPY(v);

    if (Is_Light_Null(v) or Is_Ghost(v))
        return COPY(v);

    return DELEGATE_BRANCH(OUT, branch, v);
}


//
//  else: infix:defer native [  ; NOTE - INFIX:DEFER
//
//  "If LEFT is NULL or GHOST!, return EVAL BRANCH, else return LEFT"
//
//      return: [any-value?]
//      ^left [<null> ghost! any-value?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(ELSE)
{
    INCLUDE_PARAMS_OF_ELSE;

    Value* left = ARG(LEFT);
    Stable* branch = ARG(BRANCH);

    if (Is_Error(left))
        return COPY(left);

    if (not (Is_Light_Null(left) or Is_Ghost(left)))
        return COPY(left);

    return DELEGATE_BRANCH(OUT, branch, left);
}


//
//  also: infix:defer native [  ; NOTE - INFIX:DEFER
//
//  "If LEFT is NULL or GHOST!, return it, else EVAL BRANCH but return LEFT"
//
//      return: [any-value?]
//      ^left [<null> ghost! any-value?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(ALSO)
{
    INCLUDE_PARAMS_OF_ALSO;  // `then func [x] [(...) :x]` => `also [...]`

    Value* left = ARG(LEFT);
    Stable* branch = ARG(BRANCH);

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

    if (Is_Error(left))
        return COPY(left);

    if (Is_Light_Null(left) or Is_Ghost(left))
        return COPY(left);

    STATE = ST_ALSO_RUNNING_BRANCH;
    return CONTINUE_BRANCH(OUT, branch, left);

} discard_branch_result_in_out_and_return_input: {  //////////////////////////

    dont(UNUSED(OUT));  // would corrupt the OUT pointer itself

    return COPY(left);
}}


#define LEVEL_FLAG_SAW_NON_VOID  LEVEL_FLAG_MISCELLANEOUS


typedef enum {
    NATIVE_IS_ANY,
    NATIVE_IS_ALL,
    NATIVE_IS_NONE_OF
} WhichAnyAllNone;


// ANY and ALL were very similar, and it made sense to factor their common
// code out into a single function.
//
Bounce Any_All_None_Native_Core(Level* level_, WhichAnyAllNone which)
{
    INCLUDE_PARAMS_OF_ALL;

    Element* block = Element_ARG(BLOCK);
    Option(Stable*) predicate = ARG(PREDICATE);

    Stable* condition;  // will be found in OUT or SCRATCH

    enum {
        ST_ANY_ALL_NONE_INITIAL_ENTRY = STATE_0,
        ST_ANY_ALL_NONE_EVAL_STEP,
        ST_ANY_ALL_NONE_PREDICATE
    };

    switch (STATE) {
      case ST_ANY_ALL_NONE_INITIAL_ENTRY: goto initial_entry;
      case ST_ANY_ALL_NONE_EVAL_STEP: goto eval_step_result_in_spare;
      case ST_ANY_ALL_NONE_PREDICATE: goto predicate_result_in_scratch;
      default: assert(false);
    }

  initial_entry: { ///////////////////////////////////////////////////////////

    assert(Not_Level_Flag(LEVEL, SAW_NON_VOID));

    Executor* executor;
    if (Is_Pinned_Form_Of(BLOCK, block))
        executor = &Inert_Stepper_Executor;
    else {
        assert(Is_Block(block));
        executor = &Stepper_Executor;
    }

    Flags flags = (
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
            | LEVEL_FLAG_AFRAID_OF_GHOSTS
    );

    require (
      Level* sub = Make_Level_At(executor, block, flags)
    );
    Push_Level_Erase_Out_If_State_0(SPARE, sub);

    if (Is_Level_At_End(sub))
        goto reached_end;

    STATE = ST_ANY_ALL_NONE_EVAL_STEP;
    return CONTINUE_SUBLEVEL(sub);

} eval_step_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Ghost(SPARE))  // no vote...ignore and continue
        goto next_eval_step;

    Set_Level_Flag(LEVEL, SAW_NON_VOID);

    require (
      Stable* spare = Decay_If_Unstable(SPARE)
    );

    if (predicate)
        goto run_predicate_on_eval_product;  // NONEs passed to predicate

    condition = spare;  // w/o predicate, `condition` is eval result
    goto process_condition;

} run_predicate_on_eval_product: {  //////////////////////////////////////////

    // 1. The predicate-running is pushed over the "keepalive" stepper, but we
    //    don't want the stepper to take a step before coming back to us.
    //    Temporarily patch out the Stepper_Executor() so we get control
    //    back without that intermediate step.

    SUBLEVEL->executor = &Just_Use_Out_Executor;  // tunnel thru [1]

    STATE = ST_ANY_ALL_NONE_PREDICATE;
    return CONTINUE(SCRATCH, unwrap predicate, SPARE);

} predicate_result_in_scratch: {  ////////////////////////////////////////////

    // 1. The only way a falsey evaluation should make it to the end is if a
    //    predicate let it pass.  Don't want that to trip up `if all` so make
    //    it heavy...but this way `(all:predicate [null] not?/) then [<runs>]`

    if (Any_Void(SCRATCH))  // !!! should void predicate results opt-out?
        panic (Error_Bad_Void());

    if (Is_Light_Null(SCRATCH))  // predicates can approve null [1]
        Init_Heavy_Null(SCRATCH);

    SUBLEVEL->executor = &Stepper_Executor;  // done tunneling [2]
    STATE = ST_ANY_ALL_NONE_EVAL_STEP;

    require (
      condition = Decay_If_Unstable(SCRATCH)
    );

} process_condition: {

    require (
      bool cond = Test_Conditional(condition)
    );

    switch (which) {
      case NATIVE_IS_ANY:
        if (cond)
            goto return_spare;  // successful ANY clause returns the value
        break;

      case NATIVE_IS_ALL:
        if (not cond)
            goto return_null;  // failed ALL clause returns null
        Move_Value(OUT, SPARE);  // leaves SPARE as fresh...good for next step
        break;

      case NATIVE_IS_NONE_OF:
        if (cond)
            goto return_null;  // succeeding NONE-OF clause returns null
        break;
    }

    goto next_eval_step;

} next_eval_step: { //////////////////////////////////////////////////////////

    assert(STATE == ST_ANY_ALL_NONE_EVAL_STEP);

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;

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

    if (Not_Level_Flag(LEVEL, SAW_NON_VOID))
        return GHOST;  // return void if all evaluations vaporized [1]

    switch (which) {
      case NATIVE_IS_ANY:
        return NULLED;  // non-vanishing expressions, but none of them passed

      case NATIVE_IS_ALL:
        return OUT_BRANCHED;  // successful ALL returns the last value

      case NATIVE_IS_NONE_OF:
        return OKAY;  // successful NONE-OF has no value to return, use OKAY

      default:  // some C compilers don't seem to know this is unreachable
        crash (nullptr);
    }

} return_spare: { ////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    Move_Value(OUT, SPARE);
    return OUT_BRANCHED;

} return_null: { /////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    return NULLED;
}}


//
//  all: native [
//
//  "Short-circuiting variant of AND, using a block of expressions as input"
//
//      return: [<null> ghost! any-stable?]
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
//      return: [<null> ghost! any-stable?]
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
//  none_of: native [
//
//  "Short-circuiting shorthand for NOT ALL"
//
//      return: [<null> ghost! any-stable?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! @block!]
//      :predicate "Test for whether an evaluation passes (default is DID)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(NONE_OF)
{
    return Any_All_None_Native_Core(LEVEL, NATIVE_IS_NONE_OF);
}


//
//  case: native [
//
//  "Evaluates each condition, and when non-NULL, evaluates what follows it"
//
//      return: [any-stable? heavy-null?]
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
    Option(Stable*) predicate = ARG(PREDICATE);

    enum {
        ST_CASE_INITIAL_ENTRY = STATE_0,
        ST_CASE_CONDITION_EVAL_STEP,
        ST_CASE_RUNNING_PREDICATE,
        ST_CASE_RUNNING_BRANCH
    };

    switch (STATE) {
      case ST_CASE_INITIAL_ENTRY: goto initial_entry;
      case ST_CASE_CONDITION_EVAL_STEP: goto condition_result_in_spare;
      case ST_CASE_RUNNING_PREDICATE: goto predicate_result_in_spare;
      case ST_CASE_RUNNING_BRANCH: goto branch_result_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    require (
      Level* L = Make_Level_At(
        &Stepper_Executor,
        cases,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    ));

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

} condition_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Ghost(SPARE))  // skip over things like ELIDE, but not voids!
        goto handle_next_clause;

    require (
      Decay_If_Unstable(SPARE)
    );

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;  // we tolerate "fallout" from a condition

    if (not predicate)
        goto processed_result_in_spare;

    STATE = ST_CASE_RUNNING_PREDICATE;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    return CONTINUE(SPARE, unwrap predicate, SPARE);  // with == out is legal

} predicate_result_in_spare: {  //////////////////////////////////////////////

    // 1. Expressions between branches are allowed to vaporize via GHOST
    //    (e.g. ELIDE), but voids are not skipped.
    //
    //        >> condition: null
    //        >> case [opt if condition [<a>] [print "Whoops?"] [<hmm>]]
    //        Whoops?
    //        == <hmm>

    if (Any_Void(SPARE))  // error on void predicate results (not same as [1])
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

    require (
      Stable* spare = Decay_If_Unstable(SPARE)
    );
    require (
      bool cond = Test_Conditional(spare)
    );

    Element* branch = Copy_Cell_May_Bind(
        SCRATCH, At_Level(SUBLEVEL), Level_Binding(SUBLEVEL)
    );
    Inherit_Const(branch, cases);  // branch needs to respect const [1]

    Fetch_Next_In_Feed(SUBLEVEL->feed);

    if (not cond) {
        if (not Any_Branch(branch))  // like IF [1]
            panic (Error_Bad_Value_Raw(branch));  // stable

        goto handle_next_clause;
    }

    STATE = ST_CASE_RUNNING_BRANCH;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    return CONTINUE_BRANCH(OUT, branch, SPARE);

} branch_result_in_out: {  ///////////////////////////////////////////////////

    if (not ARG(ALL)) {
        Drop_Level(SUBLEVEL);
        return OUT_BRANCHED;
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

    assert(ARG(ALL) or Is_Cell_Erased(OUT));  // never ran branch, or :ALL

    Drop_Level(SUBLEVEL);

    if (Not_Cell_Erased(SPARE)) {  // prioritize fallout result [1]
        Move_Value(OUT, SPARE);
        return OUT_BRANCHED;
    }

    if (Is_Cell_Erased(OUT))  // none of the clauses of an :ALL ran a branch
        return Init_Nulled(OUT);

    return OUT_BRANCHED;
}}


//
//  switch: native [
//
//  "Selects a choice and evaluates the block that follows it"
//
//      return: [any-stable? heavy-null?]
//      value [any-stable?]
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

    Stable* left = ARG(VALUE);
    Element* cases = Element_ARG(CASES);
    Stable* predicate;  // enforced as EQUAL? by initial_entry if none given

    enum {
        ST_SWITCH_INITIAL_ENTRY = STATE_0,
        ST_SWITCH_EVALUATING_RIGHT,
        ST_SWITCH_RUNNING_BRANCH
    };

    if (STATE == ST_SWITCH_INITIAL_ENTRY)
        goto initial_entry;

    predicate = unwrap ARG(PREDICATE);

    switch (STATE) {

      case ST_SWITCH_EVALUATING_RIGHT:
        goto right_result_in_spare;

      case ST_SWITCH_RUNNING_BRANCH:
        if (not ARG(ALL)) {
            Drop_Level(SUBLEVEL);
            return OUT_BRANCHED;
        }
        goto next_switch_step;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. Originally this called the "guts" of comparison by default, instead
  //    of invoking the EQUAL? native.  But comparison guts are no longer
  //    available without a frame.  So really this just needs to be worked on
  //    and sped up, such as to create one frame and reuse it over and over.

    assert(Is_Cell_Erased(SPARE));  // initial condition
    assert(Is_Cell_Erased(OUT));  // if no writes to out performed, we act void

    if (ARG(TYPE) and ARG(PREDICATE))
        panic (Error_Bad_Refines_Raw());

    predicate = opt ARG(PREDICATE);
    if (not predicate) {
        predicate = Copy_Cell(LOCAL(PREDICATE), LIB(EQUAL_Q));  // default [1]
        LIFT_BYTE(predicate) = NOQUOTE_2;
    }

    require (
      Level* sub = Make_Level_At(
        &Stepper_Executor,
        cases,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    ));

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

} right_result_in_spare: {  //////////////////////////////////////////////////

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

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;  // nothing left, so drop frame and return

    require (  // predicate decays?
      Stable* spare = Decay_If_Unstable(SPARE)
    );

    if (ARG(TYPE)) {
        if (not Is_Datatype(spare) and not Is_Action(spare))
            panic ("switch:type conditions must be DATATYPE! or ACTION!");

        if (not Typecheck_Uses_Spare_And_Scratch(  // *sublevel*'s SPARE!
            SUBLEVEL, left, spare, SPECIFIED  // ...so passing L->spare ok
        )){
            goto next_switch_step;
        }
    }
    else {
        assert(predicate);

        if (rebRunThrows(
            SCRATCH,  // <-- output cell
            predicate,
                rebQ(left),  // first arg (left hand side if infix)
                rebQ(spare)  // second arg (right side if infix)
        )){
            return BOUNCE_THROWN;  // aborts sublevel
        }

        require (
          Stable* scratch = Decay_If_Unstable(SCRATCH)
        );
        require (
          bool cond = Test_Conditional(scratch)
        );

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

    Element* scratch = Copy_Cell_May_Bind(SCRATCH, at, Level_Binding(SUBLEVEL));
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

    assert(ARG(ALL) or Is_Cell_Erased(OUT));

    Drop_Level(SUBLEVEL);

    if (Not_Cell_Erased(SPARE)) {  // something counts as fallout [1]
        possibly(Not_Cell_Stable(SPARE));
        Move_Value(OUT, SPARE);
        return OUT_BRANCHED;
    }

    if (Is_Cell_Erased(OUT))  // no fallout, and no branches ran
        return Init_Nulled(OUT);

    return OUT_BRANCHED;
}}


//
//  default: infix native [
//
//  "If TARGET is [NULL TRASH BLANK], set it to BRANCH eval result"
//
//      return: [any-stable?]
//      @target "Word or path which might be set (or not)"  ; on left
//          [
//              word!: tuple!: ^word!: ^tuple!:
//              /word!:  ; meta form meaningless?
//              ; group!: ^group!:  ; TBD...
//          ]
//      @branch [any-branch?]
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
    Stable* branch = ARG(BRANCH);

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
  // 3. TRASH!, GHOST!, NULL, empty PACK! and empty SPLICE! are "defaultable".
  //    Space runes (blanks) aren't; no stable form is overwritten by DEFAULT.

    Element* steps = u_cast(Element*, SCRATCH);  // avoid double-eval [1]
    STATE = ST_DEFAULT_GETTING_TARGET;  // can't leave at STATE_0

    bool slashed = false;
    if (Is_Set_Run_Word(target)) {
        assume (
          Unsingleheart_Sequence(target)  // make it into a plain set-word
        );
        slashed = true;  // so we put the slash back on
    }

    assert(not Sigil_Of(target) or Sigil_Of(target) == SIGIL_META);
    assume (
      Unsingleheart_Sequence_Preserve_Sigil(target)
    );

    Element* scratch_var = Copy_Cell(SCRATCH, target);
    Clear_Cell_Sigil(scratch_var);
    Add_Cell_Sigil(scratch_var, SIGIL_META);  // for the fetch, always use ^META

    if (slashed) { assume (
        Blank_Head_Or_Tail_Sequencify(  // put slash back for the write
            target, TYPE_PATH, CELL_FLAG_LEADING_SPACE
        )
    );}

    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (
        Get_Var_In_Scratch_To_Out(level_, steps)
    );

    if (not Any_Void(OUT)) {
        require (  // may need decay [2]
            Stable* out = Decay_If_Unstable(OUT)
        );
        if (not (Is_Trash(out) or Is_Nulled(out) or Is_None(out)))
            return OUT;  // consider it a "value" [3]
    }

    STATE = ST_DEFAULT_EVALUATING_BRANCH;
    return CONTINUE(OUT, branch, OUT);

} branch_result_in_out: {  ///////////////////////////////////////////////////

    assert(Is_Pinned(Known_Element(SCRATCH)));  // steps is the "var" to set
    heeded (Corrupt_Cell_If_Needful(SPARE));

    Set_Var_In_Scratch_To_Out(LEVEL, NO_STEPS) except (Error* e) {
        assert(false);  // shouldn't be able to happen (steps is pinned)
        panic (e);
    }
    return OUT;
}}


//
//  maybe: infix native [
//
//  "If right side VALUE is not NULL, update the left hand TARGET with it"
//
//      return: [any-stable?]
//      @target "Word or tuple which might be set (or not)"
//          [group!: word!: tuple!:]  ; should do set-block!, etc [1]
//      ^value "Quantity used to overwrite the left if not null"
//          [any-value?]  ; to do set-block! etc. needs to take PACK!
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
    Value* v = ARG(VALUE);

    if (Is_Error(v))
        return COPY(v);  // pass through but don't assign anything

    assume (
        Unsingleheart_Sequence(target)  // drop the colon off the end
    );
    Element* quoted_target = Quote_Cell(target);

    if (Is_Light_Null(v))
        return rebDelegate("get meta", quoted_target);

    Element* lifted = Lift_Cell(v);

    return rebDelegate(CANON(SET), quoted_target, lifted);  // may decay
}


//
//  catch*: native [
//
//  "Catches a throw from a block and returns its value, GHOST! if no throw"
//
//      return: [any-value? ghost!]
//      name "Name of the THROW construct to define in the block of code"
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
    return CONTINUE(OUT, block);

} code_result_in_out: {  //////////////////////////////////////////////////////

    if (not THROWING) {
        require (
          Elide_Unless_Error_Including_In_Packs(OUT)
        );
        return GHOST;  // no throw means just return ghost (pure, for ELSE)
    }

    const Stable* label = VAL_THROWN_LABEL(LEVEL);
    if (not Any_Context(label))
        return THROWN;  // not a context throw, not from DEFINITIONAL-THROW

    VarList* throw_varlist = Cell_Varlist(label);
    if (throw_varlist != Varlist_Of_Level_Maybe_Unmanaged(catch_level))
        return THROWN;  // context throw, but not to this CATCH*, keep going

    CATCH_THROWN(OUT, level_); // thrown value
    dont(Force_Cell_Heavy(OUT));  // we don't tamper with thrown value
    return OUT;
}}


//
//  definitional-throw: native [
//
//  "Throws control back to a previous catch"
//
//      return: []
//      ^value "What CATCH will receive (unstable antiforms ok, e.g. ERROR!)"
//          [any-value?]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_THROW)
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_THROW;

    Value* v = ARG(VALUE);

    Level* throw_level = LEVEL;  // Level of this RETURN call

    Option(VarList*) coupling = Level_Coupling(throw_level);
    if (not coupling)
        panic (Error_Archetype_Invoked_Raw());

    Element* label = Init_Frame(
        SCRATCH, cast(ParamList*, unwrap coupling), ANONYMOUS, UNCOUPLED
    );
    Init_Thrown_With_Label(LEVEL, v, label);
    return BOUNCE_THROWN;
}
