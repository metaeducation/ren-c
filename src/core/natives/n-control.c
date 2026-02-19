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
// * If they do not run any branches, they return VOID!.  This will signal
//   functions like ELSE and THEN.
//
// * If a branch *does* run--and its evaluation *happens* to produce VOID!,
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


// Shared helper for CONDITIONAL or OPTIONAL.
//
// !!! Because this decays it means COND and OPT can't be intrinsics.  It's
// probably worth it to figure out how to make them intrinsics by adding
// special support for their needs.
//
// 1. We want things like (cond if ...) or (cond switch ...) to work, with the
//    idea that you effectively adapt the VOID!-returning control structure
//    to be a VETO-returning control structure (without needing to come up
//    with a different name).  It's worth the slight impurity of "reacting
//    to VOID! conditionally".
//
// 2. false result makes VOID! if OPT, VETO if COND
//
static Result(bool) Return_Out_For_Conditional_Or_Optional(Level* level_)
{
    INCLUDE_PARAMS_OF_CONDITIONAL;  // WHEN and OPTIONAL must be compatible

    Value* v = Possibly_Unstable(ARG(VALUE));

    if (Is_Failure(v))
        return fail (Cell_Error(v));

    if (Is_Void(v))  // [1]
        return false;  // [2]

    Copy_Cell(OUT, v);  // save (in case pack!, we want to return the pack)

    require (
      Stable* stable = Decay_If_Unstable(v)
    );

    return Logical_Test(stable);  // [2]
}


//
//  /conditional: native [  ; cant be intrinsic (we need to decay)
//
//  "If VALUE is NULL or VOID, make it VETO, else passthru"
//
//      return: [any-value? veto?]
//      ^value '[any-value?]  ; what should CONDITIONAL do with VETO?
//  ]
//
DECLARE_NATIVE(CONDITIONAL)  // usually used via its alias of COND
{
    INCLUDE_PARAMS_OF_CONDITIONAL;

    trap (
      bool return_out = Return_Out_For_Conditional_Or_Optional(LEVEL)
    );
    if (return_out)
        return OUT_BRANCHED;

    return Copy_Cell(OUT, Lib_Value(SYM_VETO));
}


//
//  /optional: native [  ; cant be intrinsic (we need to decay)
//
//  "If VALUE is NULL or ANY-VOID?, make it VOID! else passthru"
//
//      return: [any-value? void!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(OPTIONAL)
{
    INCLUDE_PARAMS_OF_OPTIONAL;

    trap (
      bool return_out = Return_Out_For_Conditional_Or_Optional(LEVEL)
    );
    if (return_out)
        return OUT;

    return VOID_OUT_UNBRANCHED;
}


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

    Value* spare_with = SPARE;  // passed to branch if run [1]
    possibly(Is_Cell_Erased(spare_with));

    enum {
        ST_GROUP_BRANCH_INITIAL_ENTRY = STATE_0,
        ST_GROUP_BRANCH_RUNNING_GROUP,
        ST_GROUP_BRANCH_RUNNING_BRANCH  // no DELEGATE() for executors
    };

    switch (STATE) {
      case ST_GROUP_BRANCH_INITIAL_ENTRY: goto initial_entry;
      case ST_GROUP_BRANCH_RUNNING_GROUP: goto group_result_in_scratch;
      case ST_GROUP_BRANCH_RUNNING_BRANCH: return OUT;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. The Trampoline has some sanity checking asserts that try to stop you
  //    from making mistakes.  Because this does something weird to use the
  //    OUT cell as `with` the LEVEL_FLAG_FORCE_HEAVY_BRANCH was taken off at
  //    the callsite.

    if (Is_Cell_Erased(spare_with))
        Init_Null(spare_with);

    require (
      Level* sub = Make_Level(
        &Evaluator_Executor,
        LEVEL->feed,
        (not LEVEL_FLAG_FORCE_HEAVY_BRANCH)
            | (not LEVEL_FLAG_VANISHABLE_VOIDS_ONLY)  // all voids act same
    ));
    definitely(Is_Cell_Erased(SCRATCH));  // we are in STATE_0
    Push_Level(SCRATCH, sub);

    STATE = ST_GROUP_BRANCH_RUNNING_GROUP;
    return CONTINUE_SUBLEVEL;

} group_result_in_scratch: {  ////////////////////////////////////////////////

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

    if (Any_Void(SCRATCH))  // void branches giving their input is useful  [1]
        return Copy_Cell(OUT, spare_with);

    require (
      Stable* scratch_branch = Decay_If_Unstable(SCRATCH)
    );
    if (Is_Group(scratch_branch))
        panic (Error_Bad_Branch_Type_Raw());  // stop recursions (good?)

    if (Is_Antiform(scratch_branch))
        panic (Error_Bad_Antiform(scratch_branch));

    STATE = ST_GROUP_BRANCH_RUNNING_BRANCH;
    return CONTINUE(OUT, As_Element(scratch_branch), spare_with);
}}


//
//  /if: native [
//
//  "If CONDITION is not NULL, execute BRANCH, otherwise return VOID!"
//
//      return: [any-value? void!]
//      condition [any-stable?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(IF)
{
    INCLUDE_PARAMS_OF_IF;

    Stable* condition = ARG(CONDITION);
    Element* branch = ARG(BRANCH);

    if (not Logical_Test(condition))
        return VOID_OUT_UNBRANCHED;  // "light" void (triggers ELSE)

    return DELEGATE_BRANCH(OUT, branch, condition);  // branch semantics [A]
}


//
//  /either: native [
//
//  "When CONDITION is NULL, run NULL-BRANCH, else run OKAY-BRANCH"
//
//      return: [any-value? void! heavy-null?]
//      condition [any-stable?]
//      @okay-branch [any-branch?]
//      @null-branch [any-branch?]
//  ]
//
DECLARE_NATIVE(EITHER)
{
    INCLUDE_PARAMS_OF_EITHER;

    Stable* condition = ARG(CONDITION);

    Element* branch = Logical_Test(condition)
        ? ARG(OKAY_BRANCH)
        : ARG(NULL_BRANCH);

    return DELEGATE_BRANCH(OUT, branch, condition);  // branch semantics [A]
}


//
//  /did: pure native:intrinsic [
//
//  "Test for NOT void, 'light' null, or failure! (IF DID is prefix THEN)"
//
//      return: [logic!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(DID_1)
//
// 1. THEN runs on PACK! even if it contains errors.  That's not considered
//    to be an error.  So we are consistent with that: it counts as "DID"
//    for our purposes here.
{
    INCLUDE_PARAMS_OF_DID_1;

    Value* v = Possibly_Unstable(ARG(VALUE));

    if (Is_Light_Null(v) or Is_Void(v) or Is_Failure(v))
        return LOGIC_OUT(false);

    possibly(Is_Pack(v) and Is_Lifted_Failure(List_Item_At(v)));  // [1]

    return LOGIC_OUT(true);
}


//
//  /didn't: pure native:intrinsic [
//
//  "Test for void, 'light' null, or failure! (IF DIDN'T is prefix ELSE)"
//
//      return: [logic!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(DIDNT)
//
// Written in terms of DID just to emphasize consistency in the logic.
{
    INCLUDE_PARAMS_OF_DIDNT;

    Bounce b = Apply_Cfunc(NATIVE_CFUNC(DID_1), LEVEL);
    if (b == BOUNCE_OKAY)
        return LOGIC_OUT(false);

    assert(b == nullptr);
    return LOGIC_OUT(true);
}


//
//  /then: infix:defer native [  ; NOTE - INFIX:DEFER
//
//  "If LEFT is NULL or VOID!, return it, otherwise return EVAL BRANCH"
//
//      return: [any-value?]
//      ^left [<null> void! any-value?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(THEN)
{
    INCLUDE_PARAMS_OF_THEN;

    Value* left = Possibly_Unstable(ARG(LEFT));
    Element* branch = ARG(BRANCH);

    if (Is_Failure(left))
        return COPY_TO_OUT(left);

    if (Is_Light_Null(left) or Is_Void(left))
        return COPY_TO_OUT(left);

    return DELEGATE_BRANCH(OUT, branch, left);
}


//
//  /thence: native [
//
//  "If VALUE is NULL or VOID!, return it, otherwise return EVAL BRANCH"
//
//      return: [any-value?]
//      @branch [any-branch?]
//      ^value [<null> void! any-value?]
//  ]
//
DECLARE_NATIVE(THENCE)
{
    INCLUDE_PARAMS_OF_THENCE;

    Element* branch = ARG(BRANCH);
    Value* v = Possibly_Unstable(ARG(VALUE));

    if (Is_Failure(v))
        return COPY_TO_OUT(v);

    if (Is_Light_Null(v) or Is_Void(v))
        return COPY_TO_OUT(v);

    return DELEGATE_BRANCH(OUT, branch, v);
}


//
//  /else: infix:defer native [  ; NOTE - INFIX:DEFER
//
//  "If LEFT is NULL or VOID!, return EVAL BRANCH, else return LEFT"
//
//      return: [any-value?]
//      ^left [<null> void! any-value?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(ELSE)
{
    INCLUDE_PARAMS_OF_ELSE;

    Value* left = Possibly_Unstable(ARG(LEFT));
    Element* branch = ARG(BRANCH);

    if (not (Is_Light_Null(left) or Is_Void(left) or Is_Failure(left)))
        return COPY_TO_OUT(left);

    possibly(Is_Failure(left));  // handler must take ^META arg, or will panic

    return DELEGATE_BRANCH(OUT, branch, left);
}


//
//  /also: infix:defer native [  ; NOTE - INFIX:DEFER
//
//  "If LEFT is a FAILURE!, return it, else EVAL BRANCH but return LEFT"
//
//      return: [any-value?]
//      ^left [failure! any-value?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(ALSO)
{
    INCLUDE_PARAMS_OF_ALSO;  // `then func [x] [(...) :x]` => `also [...]`

    Value* left = Possibly_Unstable(ARG(LEFT));
    Element* branch = ARG(BRANCH);

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

    if (Is_Failure(left) or Is_Hot_Potato(left))
        return COPY_TO_OUT(left);

    STATE = ST_ALSO_RUNNING_BRANCH;
    return CONTINUE_BRANCH(OUT, branch, left);

} discard_branch_result_in_out_and_return_input: {  //////////////////////////

    dont(UNUSED(OUT));  // would corrupt the OUT pointer itself

    return COPY_TO_OUT(left);
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
    Option(Element*) predicate = ARG(PREDICATE);

    Value* scratch_or_spare_condition;  // SCRATCH if predicate result

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

  // 1. If you write (^x: all [<expr> ^value]) and ^value is a VOID!, you
  //    want the safety of the eval step to turn that void into a heavy
  //    void which will cause the ANY to panic, so you don't assign <expr>
  //    to X.  Only vanishable functions should disappear by default.
  //
  // 2. If ANY/ALL succeed, they can't return VOID! or light null because
  //    that wouldn't be usable with ELSE/THEN.  But it wants to see VOID!,
  //    so it has to take responsibility for its own heavy results vs.
  //    relying on LEVEL_FLAG_FORCE_HEAVY_BRANCH.

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
            | LEVEL_FLAG_VANISHABLE_VOIDS_ONLY  // safety for last step [1]
            | (not LEVEL_FLAG_FORCE_HEAVY_BRANCH)  // must see voids [2]
    );

    require (
      Level* sub = Make_Level_At(executor, block, flags)
    );
    definitely(Is_Cell_Erased(SPARE));  // we are in STATE_0
    Push_Level(SPARE, sub);

    if (Is_Level_At_End(sub))
        goto reached_end;

    STATE = ST_ANY_ALL_NONE_EVAL_STEP;
    return CONTINUE_SUBLEVEL;

} eval_step_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Void(SPARE))  // no vote...ignore and continue
        goto next_eval_step;  // means we skip BLANK! (e.g. comma), review

    if (predicate)
        goto run_predicate_on_eval_product;  // NONEs passed to predicate

    scratch_or_spare_condition = SPARE;
    goto process_condition;

} run_predicate_on_eval_product: {  //////////////////////////////////////////

  // 1. The predicate-running is pushed over the "keepalive" stepper, but we
  //    don't want the stepper to take a step before coming back to us.
  //    Temporarily patch out the Stepper_Executor() so we get control back
  //    without that intermediate step.
  //
  // 2. The predicate allows you to return VOID! and opt out of voting one
  //    way or another.  (Heavy void can't decay for the Logical_Test())

    SUBLEVEL->executor = &Just_Use_Out_Executor;  // tunnel thru [1]

    STATE = ST_ANY_ALL_NONE_PREDICATE;
    return CONTINUE(SCRATCH, unwrap predicate, SPARE);  // not branch [2]

} predicate_result_in_scratch: {  ////////////////////////////////////////////

  // 1. The only way a falsey evaluation should make it to the end is if a
  //    predicate let it pass.  Don't want that to trip up `if all` so make
  //    it heavy...but this way `(all:predicate [null] not?/) then [<runs>]`

    if (Is_Void(SCRATCH))  // allow void predicates to be ignored
        goto next_eval_step;  // (heavy voids will cause an error)

    SUBLEVEL->executor = &Stepper_Executor;  // done tunneling [2]
    STATE = ST_ANY_ALL_NONE_EVAL_STEP;
    scratch_or_spare_condition = SCRATCH;
    goto process_condition;

} process_condition: { ///////////////////////////////////////////////////////

    Copy_Cell(OUT, SPARE);  // not VOID!; save for return before decay

    require (
      Stable* condition = Decay_If_Unstable(scratch_or_spare_condition)
    );

    Set_Level_Flag(LEVEL, SAW_NON_VOID);

    bool logic = Logical_Test(condition);

    switch (which) {
      case NATIVE_IS_ANY:
        if (logic)
            goto return_out;  // successful ANY clause returns the value
        break;

      case NATIVE_IS_ALL:
        if (not logic)
            goto return_null;  // failed ALL clause returns null
        Erase_Cell(SPARE);  // freshen SPARE for next step
        break;

      case NATIVE_IS_NONE_OF:
        if (logic)
            goto return_null;  // succeeding NONE-OF clause returns null
        break;
    }

    goto next_eval_step;

} next_eval_step: { //////////////////////////////////////////////////////////

    assert(STATE == ST_ANY_ALL_NONE_EVAL_STEP);

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;

    Reset_Stepper_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL;

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
  //
  //    !!! It's clearly the right answer for (all []) to return VOID!, but
  //    while more flexibility is provided by (any []) returning it there may
  //    be good reasons to return NULL_OUT.  This is still under review.

    Drop_Level(SUBLEVEL);

    if (Not_Level_Flag(LEVEL, SAW_NON_VOID))
        return VOID_OUT_UNBRANCHED;  // void if all evaluations vaporized [1]

    switch (which) {
      case NATIVE_IS_ANY:
        return NULL_OUT;  // non-vanishing expressions, but none of them passed

      case NATIVE_IS_ALL:  // successful ALL returns the last value
        return Force_Cell_Heavy(OUT);  // didn't CONTINUE_BRANCH(), saw voids

      case NATIVE_IS_NONE_OF:
        return BOUNCE_OKAY;  // successful NONE-OF has no value to return

      default:  // some C compilers don't seem to know this is unreachable
        crash (nullptr);
    }

} return_out: { //////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    return Force_Cell_Heavy(OUT);  // see LEVEL_FLAG_FORCE_HEAVY_BRANCH notes

} return_null: { /////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    return NULL_OUT;
}}


//
//  /all: native [
//
//  "Short-circuiting variant of AND, using a block of expressions as input"
//
//      return: [<null> void! any-value?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! @block!]
//      :predicate "Test applied to each eval step (default is LOGICAL)"
//          [frame!]
//  ]
//
DECLARE_NATIVE(ALL)
{
    return Any_All_None_Native_Core(LEVEL, NATIVE_IS_ALL);
}


//
//  /any: native [
//
//  "Short-circuiting version of OR, using a block of expressions as input"
//
//      return: [<null> void! any-value?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! @block!]
//      :predicate "Test applied to each eval step (default is LOGICAL)"
//          [frame!]
//  ]
//
DECLARE_NATIVE(ANY)
{
    return Any_All_None_Native_Core(LEVEL, NATIVE_IS_ANY);
}


//
//  /none-of: native [
//
//  "Short-circuiting shorthand for NOT ALL"
//
//      return: [<null> void! any-value?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! @block!]
//      :predicate "Test applied to each eval step (default is LOGICAL)"
//          [frame!]
//  ]
//
DECLARE_NATIVE(NONE_OF)
{
    return Any_All_None_Native_Core(LEVEL, NATIVE_IS_NONE_OF);
}


//
//  /case: native [
//
//  "Evaluates each condition, and when non-NULL, evaluates what follows it"
//
//      return: [any-stable? heavy-null?]
//      cases "Conditions followed by branches"
//          [block!]
//      :all "Do not stop after finding first logically true case"
//      :predicate "Unary case-processing action (default is LOGICAL)"
//          [frame!]
//  ]
//
DECLARE_NATIVE(CASE)
{
    INCLUDE_PARAMS_OF_CASE;

    Element* cases = Element_ARG(CASES);
    Option(Element*) predicate = ARG(PREDICATE);

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
    definitely(Is_Cell_Erased(SPARE));  // we are in STATE_0
    Push_Level(SPARE, L);

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
    Reset_Stepper_Erase_Out(SUBLEVEL);

    return CONTINUE_SUBLEVEL;  // one step to pass predicate [1]

} condition_result_in_spare: {  //////////////////////////////////////////////

  // 1. Expressions between branches are allowed to vaporize via vanishable
  //    functions (e.g. ELIDE), but due to LEVEL_FLAG_VANISHABLE_VOIDS_ONLY,
  //    non-vanishable functions (things like `if` or `opt`) will produce
  //    HEAVY VOID, and be seen in-band as actual case values:
  //
  //        >> condition: null
  //        >> case [opt if condition [<a>] [print "Whoops?"] [<hmm>]]
  //        ** PANIC: Can't decay HEAVY VOID ~()~ for LOGICAL test

    if (Is_Void(SPARE))  // skip over ELIDE, but not non-vanishables [1]
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
  //
  // 2. We want to make sure we don't skip over things in the CASE structure
  //    that aren't branches.  But we only have to check when we skip: the
  //    branch is checked by the switch() statement in CONTINUE_BRANCH() if
  //    it is to be run.

    Element* branch = Copy_Cell_May_Bind(
        SCRATCH, At_Level(SUBLEVEL), Level_Binding(SUBLEVEL)
    );
    Inherit_Const(branch, cases);  // branch needs to respect const [1]

    Fetch_Next_In_Feed(SUBLEVEL->feed);

    require (
      Stable* spare = Decay_If_Unstable(SPARE)
    );
    if (not Logical_Test(spare)) {
        if (not Any_Branch(branch))  // CONTINUE_BRANCH does its own check [2]
            panic (Error_Bad_Value_Raw(branch));  // stable

        goto handle_next_clause;
    }

    STATE = ST_CASE_RUNNING_BRANCH;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    return CONTINUE_BRANCH(OUT, branch, SPARE);  // [2]

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
        Move_Cell(OUT, SPARE);
        return OUT_BRANCHED;
    }

    if (Is_Cell_Erased(OUT))  // none of the clauses of an :ALL ran a branch
        return NULL_OUT;

    return OUT_BRANCHED;
}}


//
//  /switch: native [
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
//          [frame!]
//  ]
//
DECLARE_NATIVE(SWITCH)
{
    INCLUDE_PARAMS_OF_SWITCH;

    Stable* left = ARG(VALUE);
    Element* cases = Element_ARG(CASES);
    Value* predicate;  // enforced as EQUAL? by initial_entry if none given

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
        LIFT_BYTE(predicate) = NOQUOTE_3;
    }

    require (
      Level* sub = Make_Level_At(
        &Stepper_Executor,
        cases,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    ));
    definitely(Is_Cell_Erased(SPARE));  // we are in STATE_0
    Push_Level(SPARE, sub);

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
    Reset_Stepper_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL;  // no direct predicate call [1]

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

    if (Is_Void(SPARE))  // skip comments or ELIDEs
        goto next_switch_step;

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;  // nothing left, so drop frame and return

    require (  // predicate decays?
      Stable* spare = Decay_If_Unstable(SPARE)
    );

    if (ARG(TYPE)) {
        if (not Is_Datatype(spare) and not Is_Frame(spare))
            panic ("switch:type conditions must be DATATYPE! or FRAME!");

        if (not Typecheck_Use_Toplevel(  // *sublevel*'s SPARE!
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
        if (not Logical_Test(scratch))
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
        Move_Cell(OUT, SPARE);
        return OUT_BRANCHED;
    }

    if (Is_Cell_Erased(OUT))  // no fallout, and no branches ran
        return NULL_OUT;

    return OUT_BRANCHED;
}}


#define CELL_FLAG_OUT_NOTE_WAS_SLASHED  CELL_FLAG_NOTE

//
//  /default: infix native [
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

    Element* target = ARG(TARGET);
    Element* branch = ARG(BRANCH);

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
  // 2. Current GET allows for FAILURE! to be returned in cases like a missing
  //    field from an OBJECT!.  This may not be a good idea, given that ^META
  //    fields can legitimately give back FAILURE! in-band if a field stored
  //    one.  It's under review.
  //
  // 3. TRASH!, VOID!, NULL, empty PACK! and empty SPLICE! are "defaultable".
  //    Space runes (blanks) aren't; no stable form is overwritten by DEFAULT.

    Element* steps = u_cast(Element*, SCRATCH);  // avoid double-eval [1]
    STATE = ST_DEFAULT_GETTING_TARGET;  // can't leave at STATE_0

    if (Is_Set_Run_Word(target)) {
        assume (
          Unsingleheart_Sequence(target)  // make it into a plain set-word
        );
        Set_Cell_Flag(target, OUT_NOTE_WAS_SLASHED);
    }

    assert(not Sigil_Of(target) or Sigil_Of(target) == SIGIL_META);
    assume (
      Unsingleheart_Sequence_Preserve_Sigil(target)
    );

    Force_Cell_Sigil(target, SIGIL_META);  // for fetch, always use ^META

  get_variable_with_steps_to_out: {

  // The Get_Var() mechanics are temporarily (?) being changed to not expose
  // the returning of "Steps" as that is a rarely-needed feature.  So for
  // now you use TWEAK if you want steps, and get a lifted result.

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Init_Null_Signifying_Tweak_Is_Pick(SCRATCH));

    STATE = ST_TWEAK_GETTING;

    Option(Error*) e = Tweak_Var_With_Dual_Scratch_To_Spare_Use_Toplevel(
        target,
        steps
    );

    if (e)
        panic (unwrap e);

    require (
      Unlift_Cell_No_Decay(SPARE)  // not unstable if wasn't ^META [1]
    );

} check_for_defaultability: {  // !!! change to use lifted value?

    if (not (Any_Void(SPARE) or Is_Trash(SPARE))) {
        require (  // may need decay [2]
            Stable* stable_spare = Decay_If_Unstable(SPARE)
        );
        if (not (Is_Null(stable_spare) or Is_None(stable_spare)))
            return COPY_TO_OUT(SPARE);  // consider it a "value" [3]
    }

    STATE = ST_DEFAULT_EVALUATING_BRANCH;
    return CONTINUE(OUT, branch, SPARE);

}} branch_result_in_out: {  ///////////////////////////////////////////////////

    possibly(Get_Cell_Flag(target, OUT_NOTE_WAS_SLASHED));  // how to honor?

    Copy_Cell(target, As_Element(SCRATCH));
    assert(Is_Tied(target));  // steps is the "var" to set

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    STATE = ST_TWEAK_SETTING;

    Set_Var_To_Out_Use_Toplevel(
        target, GROUP_EVAL_NO
    ) except (Error* e) {
        assert(false);  // shouldn't be able to happen (steps is pinned)
        panic (e);
    }
    return OUT;
}}


//
//  /maybe: infix native [
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

    Element* target = ARG(TARGET);
    Value* v = ARG(VALUE);

    if (Is_Failure(v))
        return COPY_TO_OUT(v);  // pass through but don't assign anything

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
//  /catch*: native [
//
//  "Catches a throw from a block and returns its value, VOID! if no throw"
//
//      return: [any-value? void!]
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
        require (  // error if failure before erroring on non-throw
          Ensure_No_Failures_Including_In_Packs(OUT)
        );
        panic (Error_Catch_No_Throw_Raw());
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
//  /definitional-throw: native [
//
//  "Throws VALUE to its associated CATCH (default is VOID)"
//
//      return: []
//      ^value [<hole> any-value?]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_THROW)
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_THROW;

    Param* param = ARG(VALUE);

    Value* v;
    if (Is_Cell_A_Bedrock_Hole(param))
        v = Init_Void(LOCAL(VALUE));
    else
        v = As_Value(param);

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
