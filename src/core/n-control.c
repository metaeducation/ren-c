//
//  File: %n-control.c
//  Summary: "native functions for control flow"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// * If they do not run any branches, the construct will "return void".  This
//   will signal functions like ELSE and DIDN'T, much like the NULL state
//   conveying soft failure does.
//
//   (Note: `return VOID;` doesn't actually overwrite the contents of the
//    output cell.  This makes it possible for functions like ALL to skip
//    over void results and let the previous evaluation "drop out".
//    See %cell-void.h for more information about this mechanic.)
//
// * If a branch *does* run--and its evaluation happens to produce VOID or
//   NULL, then they are wrapped in a BLOCK! antiform.  This way THEN runs
//   instead of ELSE.  Although this does mean there is some conflation of
//   the results, the conflated values have properties that mostly align with
//   what their intent was--so it works about as well as it can.
//
// * Zero-arity function values used as branches will be executed, and
//   single-arity functions used as branches will also be executed--but passed
//   the value of the triggering condition.  Especially useful with lambda:
//
//       >> if 1 < 2 [10 + 20] then x -> [print ["THEN got" x]]
//       THEN got 30
//
//   (See Do_Branch_Throws() for supported ANY-BRANCH? types and behaviors.)
//
// * There is added checking that a literal block is not used as a condition,
//   to catch common mistakes like `if [x = 10] [...]`.
//

#include "sys-core.h"


//
//  Group_Branch_Executor: C
//
// To make it easier for anything that runs a branch, the double-evaluation in
// a GROUP! branch has its own executor.  This means something like IF can
// push a level with the branch executor which can complete and then run the
// evaluated-to branch.
//
// So the group branch executor is pushed with the feed of the GROUP! to run.
// It gives this feed to an Evaluator_Executor(), and then delegates to the
// branch returned as a result.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. The `with` parameter in continuations isn't required to be GC safe or
//    even distinct from the output cell (see CONTINUE_CORE()).  So whoever
//    dispatched to the group branch executor could have passed a fleeting
//    value pointer...hence it needs to be stored somewhere.  So the group
//    executor expects it to be preloaded into `out`...or that out be marked
//    stale if it was END.  It can then take advantage of the same flexibility
//    to pass OUT as the with as well as the target when branch is in SPARE.
//
// 2. The Trampoline has some sanity checking asserts that try to stop you
//    from making mistakes.  Because this does something weird to use the
//    OUT cell as `with` the LEVEL_FLAG_BRANCH was taken off at the callsite.
//
Bounce Group_Branch_Executor(Level* level_)
{
    if (THROWING)
        return THROWN;

    switch (STATE) {
      case ST_GROUP_BRANCH_ENTRY_DONT_ERASE_OUT :
        goto initial_entry;

      case ST_GROUP_BRANCH_RUNNING_GROUP :
        goto group_result_in_spare;

      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Init_Void(PUSH());  // primed result
    Level* sub = Make_Level(
        LEVEL->feed,
        ((LEVEL->flags.bits & (~ FLAG_STATE_BYTE(255)))  // take out state 1
            | LEVEL_FLAG_BRANCH)
    );
    Push_Level(SPARE, sub);
    sub->executor = &Stepper_Executor;

    STATE = ST_GROUP_BRANCH_RUNNING_GROUP;
    return CATCH_CONTINUE_SUBLEVEL(sub);

} group_result_in_spare: {  //////////////////////////////////////////////////

    if (Any_Group(SPARE))
        fail (Error_Bad_Branch_Type_Raw());  // stop infinite recursion (good?)

    const Atom* with = Is_Fresh(OUT) ? nullptr : OUT;  // with here [1]

    assert(Is_Level_At_End(LEVEL));
    return DELEGATE_BRANCH(OUT, SPARE, with);  // couldn't do (OUT, OUT, SPARE)
}}


//
//  if: native [
//
//  "When TO LOGIC! CONDITION is true, execute branch"
//
//      return: "void if branch not run, otherwise branch result (see HEAVY)"
//          [any-atom?]
//      condition [any-value?]  ; non-void-value? possible, but slower
//      :branch "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch?]
//  ]
//
DECLARE_NATIVE(if)
//
// 1. ~false~ and ~null~ antiforms are falsey, while voids error.
//
// 2. Evaluations must be performed through continuations, so IF can't be on
//    the C function stack while the branch runs.  Rather than asking to be
//    called back after the evaluation so it can turn null and void into
//    "heavy" pack forms, it requests "branch semantics" so that the evaluator
//    does that automatically.  `delegate` means it doesn't need a callback.
{
    INCLUDE_PARAMS_OF_IF;

    Value* condition = ARG(condition);
    Value* branch = ARG(branch);

    if (Is_Falsey(condition))  // [1]
        return VOID;

    return DELEGATE_BRANCH(OUT, branch, condition);  // no callback [2]
}


//
//  either: native [
//
//  "Choose a branch to execute, based on TO-LOGIC of the CONDITION value"
//
//      return: [any-atom?]
//      condition [any-value?]  ; non-void-value? possible, but slower
//      :true-branch "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch?]
//      :false-branch
//          [any-branch?]
//  ]
//
DECLARE_NATIVE(either)
{
    INCLUDE_PARAMS_OF_EITHER;

    Value* condition = ARG(condition);

    Value* branch = Is_Truthy(condition)  // [1] on IF native
        ? ARG(true_branch)
        : ARG(false_branch);

    return DELEGATE_BRANCH(OUT, branch, condition);  // [2] on IF native
}


//=//// "THENABLE" FACILITIES: LAZY THEN AND ELSE /////////////////////////=//
//
// The conventional sense of THEN and ELSE are tied to whether a result is
// "nothing" or not, where NULL and VOID are the nothing states.
//
//    >> if false [<not run>]
//    ; void
//
//    >> if false [<not run>] else [print "ELSE triggered by voidness"]
//    ELSE triggered by voidness
//
// But this mechanical notion is augmented by a methodization concept, which
// bears some similarity to "thenable" objects in JavaScript:
//
// https://stackoverflow.com/q/59492809/
//
// 1. The input is a ^META parameter in order to react to unstable isotopes.
//    But we don't want to actually return a quoted version of the input if
//    branches don't run, so unmeta them.
//
// 2. THEN and ELSE want to pass on contents of a multi-return pack to
//    branches if applicable.  But the decision on whether it's a THEN or
//    ELSE case comes from the first parameter in the pack.
//
// 3. With the exception of ~[~null~]~ and ~[~void~]~ when /DECAY is used, a "pack"
//    (antiform block) will always run a THEN and not an ELSE.  If a function
//    wants to tweak this, it needs to return a lazy object with customized
//    then/else behavior that otherwise reifies to a pack.
//
// 4. A lazy object with a THEN or ELSE method should have that handled here.
//    But if those methods aren't present, should it be reified or passed
//    as-is to the THEN branch, which may be able to take it as ^META and
//    not reify it?
//
// 5. It is legal for a lazy object to have both THEN and ELSE handlers.
//    This is useful for instance if it wants to guarantee that at least one
//    of the tests is done (e.g. if it would reify to an error if the usage
//    did not demonstrate consciousness of the failure one way or another.)
//
//    This means a THEN method might not want to run a branch at all, and
//    instead run some code as a reaction to the knowledge that a THEN test
//    was performed.  Such cases should not be forced to a "branch was taken"
//    result, so an exception is made for the THEN hook when both a THEN and
//    ELSE hook are provided.
//
// 6. If only a THEN or an ELSE branch is received, then a desire for
//    "full control" is not assumed, and it is safer to make sure they don't
//    accidentally wind up returning something from a THEN which could
//    itself trigger an ELSE (for instance).
//

enum {
    ST_THENABLE_INITIAL_ENTRY = STATE_0,
    ST_THENABLE_REIFYING_SPARE,
    ST_THENABLE_RUNNING_BRANCH,
    ST_THENABLE_REJECTING_INPUT
};

static Bounce Then_Else_Isotopic_Object_Helper(
    Level* level_,
    bool then  // true when doing a THEN
){
    INCLUDE_PARAMS_OF_THEN;  // assume frame compatibility w/ELSE

    Atom* in = ARG(atom);  /* !!! Wrong, rewrite this routine */
    Value* branch = ARG(branch);

    if (Is_Meta_Of_Nihil(in))
        fail ("THEN/ELSE cannot operate on empty pack! input (e.g. NIHIL)");

    Meta_Unquotify_Undecayed(in);  // [1]

    if (Is_Raised(in)) {  // definitional failure, skip
        STATE = ST_THENABLE_REJECTING_INPUT;
        Copy_Cell(OUT, in);
        return OUT;
    }

    if (not Is_Lazy(in))  // Packs run THEN, including ~[~null~]~ and ~[~]~ [3]
        goto test_not_lazy;

    goto handle_lazy_object;

  handle_lazy_object: {  /////////////////////////////////////////////////////

    Option(Value*) then_hook = Select_Symbol_In_Context(in, Canon(THEN));
    if (then_hook and Is_Void(unwrap(then_hook)))
        then_hook = nullptr;  // can be unset by Debranch_Output()

    Option(Value*) else_hook = Select_Symbol_In_Context(in, Canon(ELSE));
    if (else_hook and Is_Void(unwrap(else_hook)))
        else_hook = nullptr;  // can be unset by Debranch_Output()

    if (not then_hook and not else_hook) {  // !!! should it always take THEN?
        if (not Pushed_Decaying_Level(  // fails if no reify method [4]
            SPARE,
            in,
            LEVEL_FLAG_META_RESULT
        )){
            Copy_Cell(in, SPARE);  // cheap reification... (e.g. quoted)
            Meta_Unquotify_Known_Stable(Stable_Unchecked(in));  // [1]
            assert(STATE == ST_THENABLE_INITIAL_ENTRY);
            assert(not Is_Antiform(in));
            goto test_not_lazy;
        }

        STATE = ST_THENABLE_REIFYING_SPARE;  // will call helper again
        return CONTINUE_SUBLEVEL(TOP_LEVEL);
    }

    Value* hook;
    if (then) {
        if (not then_hook) {
            STATE = ST_THENABLE_REJECTING_INPUT;
            Copy_Cell(OUT, in);  // pass lazy object thru to ELSEs
            return OUT;
        }
        hook = unwrap(then_hook);
    }
    else {
        if (not else_hook) {
            STATE = ST_THENABLE_REJECTING_INPUT;
            Copy_Cell(OUT, in);  // pass lazy object thru to THENs (?)
            return OUT;
        }
        hook = unwrap(else_hook);
    }

    STATE = ST_THENABLE_RUNNING_BRANCH;

    if (then_hook and else_hook)  // THEN is likely passthru if both
        return DELEGATE(OUT, hook, branch);  // not DELEGATE_BRANCH [5]

    if (not Is_Frame(hook))  // if not full control, assume must use BRANCH
        fail ("non-FRAME! found in THEN or ELSE method of lazy object");

    return DELEGATE_BRANCH(OUT, hook, branch);  // BRANCH for safety [6]

} test_not_lazy: {  //////////////////////////////////////////////////////////

    assert(not Is_Lazy(in));

    if (Is_Stable(in) and (Is_Void(in) or (REF(decay) and Is_Heavy_Void(in)))) {
        if (then) {
            STATE = ST_THENABLE_REJECTING_INPUT;
            if (Is_Pack(in)) {
                Copy_Cell(OUT, in);
                return OUT;
            }
            return VOID;  // then of void, don't want to write to output cell
        }
        STATE = ST_THENABLE_RUNNING_BRANCH;
        return DELEGATE_BRANCH(OUT, branch, in);  // else of void
    }

    if (Is_Nulled(in) or (REF(decay) and Is_Heavy_Null(in))) {
        if (then) {
            STATE = ST_THENABLE_REJECTING_INPUT;
            Copy_Cell(OUT, in);  // then of null, may be pack
            return OUT;
        }
        STATE = ST_THENABLE_RUNNING_BRANCH;
        return DELEGATE_BRANCH(OUT, branch, in);  // else of null
    }

    if (not then) {
        STATE = ST_THENABLE_REJECTING_INPUT;
        Copy_Cell(OUT, in);  // passthru [4]
        return OUT;
    }
    STATE = ST_THENABLE_RUNNING_BRANCH;
    return DELEGATE_BRANCH(OUT, branch, in);  // then branch, takes arg
}}


//
//  did: native [
//
//  "Tests for not being a 'pure' null or void (IF DID is prefix THEN)"
//
//      return: [logic?]
//      ^atom "Argument to test"
//          [any-atom?]
//      /decay
//      <local> branch  ; for frame compatibility with THEN/ELSE/ALSO
//  ]
//
DECLARE_NATIVE(did_1)  // see TO-C-NAME for why the "_1" is needed
//
// DID exists as a complement to antiforms to help solve conflation of falsey
// values with conditional tests.  One example:
//
//     >> match [logic? integer!] false
//     == ~false~  ; anti
//
//     >> if (match [logic? integer!] false) [print "Want this to run"]
//     ** Error: We save you by not letting antiforms be conditionally tested
//
//     >> did match [logic? integer!] false
//     == ~true~  ; DID tolerates antiforms, returns false only on true NULL
//
//     >> if (did match [logic? integer!] false) [print "Praise isotopes!"]
//     Praise isotopes!
//
// By making routines that intend to return ANY-VALUE? (even falsey ones) on
// success return the falsey ones as antiforms, incorrect uses can be caught
// and guided to use DID or DIDN'T (or whatever they actually meant).
{
    INCLUDE_PARAMS_OF_DID_1;

    Value* in = ARG(atom);
    USED(ARG(decay));  // used by helper
    USED(ARG(branch));

    switch (STATE) {
      case ST_THENABLE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_THENABLE_REIFYING_SPARE :
        assert(Is_Metaform(SPARE));
        Copy_Cell(in, cast(Element*, SPARE));
        goto reifying_input;  // multiple reifications may be needed

      case ST_THENABLE_RUNNING_BRANCH :
        goto return_true;

      default :
        assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Quotify(Quasify(Init_Word(ARG(branch), Canon(DID_1))), 1);  // [1]

    goto reifying_input;

} reifying_input: {  /////////////////////////////////////////////////////////

    bool then = true;
    Bounce bounce = Then_Else_Isotopic_Object_Helper(level_, then);

    switch (STATE) {
      case ST_THENABLE_REIFYING_SPARE:  // needs another reify step
        assert(bounce == BOUNCE_CONTINUE);
        break;

      case ST_THENABLE_REJECTING_INPUT:
        assert(bounce == OUT);
        bounce = Init_False(OUT);
        break;

      case ST_THENABLE_RUNNING_BRANCH:
        if (bounce == OUT)  // was a cheap branch, didn't need to run
            goto return_true;
        assert(bounce == BOUNCE_DELEGATE);
        bounce = BOUNCE_CONTINUE;  // let resolve code run, but we want TRUE
        break;

      default:
        assert(false);
    }

    return bounce;

} return_true: {  ////////////////////////////////////////////////////////////

    return Init_True(OUT);  // can't trust branch product [1]
}}


//
//  didn't: native [
//
//  "Test for being a 'pure' null or void (`IF DIDN'T` is prefix `ELSE`)"
//
//      return: [logic?]
//      ^atom "Argument to test"
//          [any-atom?]
//      /decay
//      <local> branch  ; for frame compatibility with THEN/ELSE/ALSO
//  ]
//
DECLARE_NATIVE(didnt)
{
    INCLUDE_PARAMS_OF_DIDNT;

    Value* in = ARG(atom);
    USED(ARG(decay));  // used by helper
    USED(ARG(branch));

    if (Is_Meta_Of_Void(in) or Is_Meta_Of_Null(in))
        return Init_True(OUT);

    if (REF(decay) and Is_Quasi_Null(in))
        return Init_True(OUT);

    return Init_False(OUT);
}


//
//  then: enfix native [
//
//  "If input is null, return null, otherwise evaluate the branch"
//
//      return: "null if input is null, or branch result"
//          [any-atom?]
//      ^atom "<deferred argument> Run branch if this is not null"
//          [any-atom?]
//      /decay
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [<unrun> any-branch?]
//  ]
//
DECLARE_NATIVE(then)  // see `tweak :then 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_THEN;

    Value* in = ARG(atom);
    Deactivate_If_Action(ARG(branch));
    USED(ARG(branch));  // used by helper
    USED(ARG(decay));

    switch (STATE) {
      case ST_THENABLE_INITIAL_ENTRY :
        goto reifying_input;

      case ST_THENABLE_REIFYING_SPARE :
        assert(Is_Metaform(SPARE));
        Copy_Cell(in, cast(Element*, SPARE));
        goto reifying_input;

      default : assert(false);
    }

  reifying_input: {  /////////////////////////////////////////////////////////

    bool then = true;
    Bounce bounce = Then_Else_Isotopic_Object_Helper(level_, then);
    return bounce;
}}


//
//  else: enfix native [
//
//  "If input is not null, return that value, otherwise evaluate the branch"
//
//      return: "Input value if not null, or branch result"
//          [any-atom?]
//      ^atom "<deferred argument> Run branch if this is null"
//          [any-atom?]
//      /decay
//      :branch [<unrun> any-branch?]
//  ]
//
DECLARE_NATIVE(else)  // see `tweak :else 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ELSE;

    Value* in = ARG(atom);
    Deactivate_If_Action(ARG(branch));
    USED(ARG(branch));  // used by helper
    USED(ARG(decay));

    switch (STATE) {
      case ST_THENABLE_INITIAL_ENTRY :
        goto reifying_input;

      case ST_THENABLE_REIFYING_SPARE :
        assert(Is_Metaform(SPARE));
        Copy_Cell(in, cast(Element*, SPARE));
        goto reifying_input;

      default : assert(false);
    }

  reifying_input: {  /////////////////////////////////////////////////////////

    bool then = false;
    Bounce bounce = Then_Else_Isotopic_Object_Helper(level_, then);
    return bounce;
}}


//
//  also: enfix native [
//
//  "For non-null input, evaluate and discard branch (like a pass-thru THEN)"
//
//      return: "The same value as input, regardless of if branch runs"
//          [any-atom?]
//      ^atom "<deferred argument> Run branch if this is not null"
//          [any-atom?]
//      /decay
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [<unrun> any-branch?]
//  ]
//
DECLARE_NATIVE(also)  // see `tweak :also 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ALSO;  // `then func [x] [(...) :x]` => `also [...]`

    Value* in = ARG(atom);
    Value* branch = ARG(branch);
    Deactivate_If_Action(ARG(branch));

    enum {
        ST_ALSO_INITIAL_ENTRY = STATE_0,
        ST_ALSO_RUNNING_BRANCH
    };

    switch (STATE) {
      case ST_ALSO_INITIAL_ENTRY: goto initial_entry;
      case ST_ALSO_RUNNING_BRANCH: goto return_original_input;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Meta_Of_Nihil(in))
        fail ("ALSO cannot operate on empty pack! input (e.g. NIHIL)");

    if (Is_Meta_Of_Void(in))
        return VOID;  // telegraph invisible intent

    if (Is_Meta_Of_Null(in))
        return nullptr;  // telegraph pure null

    if (Is_Meta_Of_Raised(in)) {  // definitional failure, skip
        Copy_Cell(OUT, in);
        Unquasify(stable_OUT);
        return Raisify(OUT);
    }

    if (REF(decay) and Is_Meta_Of_Heavy_Null(in))
        return Init_Heavy_Null(OUT);

    STATE = ST_ALSO_RUNNING_BRANCH;
    return CONTINUE(SPARE, branch, Meta_Unquotify_Undecayed(in));

} return_original_input: {  //////////////////////////////////////////////////

    return COPY(in);  // in argument has already been Meta_Unquotify()'d
}}


//
//  match: native [
//
//  "Check value using tests (match types, TRUE or FALSE, or filter action)"
//
//      return: "Input if it matched, NULL if it did not"
//          [any-value?]
//      test "Typeset or arity-1 filter function"
//          [~null~ logic? action? block! type-word! type-group! type-block!]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(match)
//
// Note: Ambitious ideas for the "MATCH dialect" are on hold, and this function
// just does some fairly simple matching:
//
//   https://forum.rebol.info/t/time-to-meet-your-match-dialect/1009/5
{
    INCLUDE_PARAMS_OF_MATCH;

    Value* v = ARG(value);
    Value* test = ARG(test);

    if (Is_Nulled(test)) {
        if (not Is_Nulled(v))
            return nullptr;
    }
    else if (Is_Logic(test)) {
        if (Is_Truthy(v) != Cell_Logic(test))
            return nullptr;
    }
    else if (Is_Action(test)) {
        if (rebRunThrows(
            cast(Value*, SPARE),  // <-- output cell, API doesn't do unstable
            rebRUN(test), rebQ(v)
        )){
            return THROWN;
        }
        if (Is_Falsey(stable_SPARE))
            return nullptr;
    }
    else switch (VAL_TYPE(test)) {
      case REB_PARAMETER:
      case REB_BLOCK:
      case REB_TYPE_WORD:
      case REB_TYPE_GROUP:
      case REB_TYPE_BLOCK:
        if (not Typecheck_Atom(test, v))
            return nullptr;
        break;

      default:
        fail (PARAM(test));  // all test types should be accounted for in switch
    }

    //=//// IF IT GOT THIS FAR WITHOUT RETURNING, THE TEST MATCHED /////////=//

    Copy_Cell(OUT, v);

    return BRANCHED(OUT);  // "heavy" null or void, so THEN and ELSE work
}


#define LEVEL_FLAG_ALL_VOIDS LEVEL_FLAG_24


//
//  all: native [
//
//  "Short-circuiting variant of AND, using a block of expressions as input"
//
//      return: "Product of last passing evaluation if all truthy, else null"
//          [any-value?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! the-block!]
//      /predicate "Test for whether an evaluation passes (default is DID)"
//          [<unrun> frame!]
//      <local> scratch
//  ]
//
DECLARE_NATIVE(all)
//
// 1. Historically there has been controversy over what should be done about
//    (all []) and (any []).  Languages that have variadic short-circuiting
//    AND + OR operations typically empty AND-ing is truthy while empty OR-ing
//    is falsey.
//
//    There are some reasonable intuitive arguments for that--*if* those are
//    your only two choices.  Because Ren-C has the option of voids, it's
//    better to signal to the caller that nothing happened.  For an example
//    of how useful it is, see the loop wrapper FOR-BOTH.  Other behaviors
//    can be forced with (all [... null]) or (any [... true])
//
// 2. The predicate-running condition gets pushed over the "keepalive" stepper,
//    but we don't want the stepper to take a step before coming back to us.
//    Temporarily patch out the Evaluator_Executor() so we get control back
//    without that intermediate step.
//
// 3. The only way a falsey evaluation should make it to the end is if a
//    predicate let it pass.  Don't want that to trip up `if all` so make it
//    heavy...but this way `(all/predicate [null] :not?) then [<runs>]`
{
    INCLUDE_PARAMS_OF_ALL;

    Value* block = ARG(block);
    Value* predicate = ARG(predicate);

    Value* scratch = ARG(scratch);

    Value* condition;  // will be found in OUT or scratch

    enum {
        ST_ALL_INITIAL_ENTRY = STATE_0,
        ST_ALL_EVAL_STEP,
        ST_ALL_PREDICATE
    };

    switch (STATE) {
      case ST_ALL_INITIAL_ENTRY: goto initial_entry;
      case ST_ALL_EVAL_STEP: goto eval_step_result_in_spare;
      case ST_ALL_PREDICATE: goto predicate_result_in_scratch;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Cell_Series_Len_At(block) == 0)
        return VOID;

    assert(Not_Level_Flag(LEVEL, ALL_VOIDS));
    Set_Level_Flag(LEVEL, ALL_VOIDS);

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;

    if (Is_The_Block(block))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;

    Level* sub = Make_Level_At(block, flags);
    Push_Level(SPARE, sub);

    STATE = ST_ALL_EVAL_STEP;
    return CONTINUE_SUBLEVEL(sub);

} eval_step_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Elision(SPARE)) {  // (comment "hi") or ,
      handle_elision:
        if (Is_Level_At_End(SUBLEVEL))
            goto reached_end;

        assert(STATE == ST_ALL_EVAL_STEP);
        Restart_Evaluator_Level(SUBLEVEL);
        return CONTINUE_SUBLEVEL(SUBLEVEL);
    }

    Decay_If_Unstable(SPARE);
    if (Is_Void(SPARE))  // (if false [<a>])
        goto handle_elision;

    Clear_Level_Flag(LEVEL, ALL_VOIDS);

    if (not Is_Nulled(predicate)) {
        SUBLEVEL->executor = &Just_Use_Out_Executor;  // tunnel thru [2]

        STATE = ST_ALL_PREDICATE;
        return CONTINUE(scratch, predicate, SPARE);
    }

    condition = stable_SPARE;  // without predicate, `condition` is same as evaluation
    goto process_condition;

} predicate_result_in_scratch: {  ////////////////////////////////////////////

    if (Is_Void(scratch))  // !!! Should void predicate results signal opt-out?
        fail (Error_Bad_Void());

    Isotopify_If_Falsey(SPARE);  // predicates can approve "falseys" [3]

    SUBLEVEL->executor = &Evaluator_Executor;  // done tunneling [2]
    STATE = ST_ALL_EVAL_STEP;

    condition = scratch;
    goto process_condition;  // with predicate, `condition` is predicate result

} process_condition: {  //////////////////////////////////////////////////////

    if (Is_Falsey(condition)) {
        Drop_Level(SUBLEVEL);
        return nullptr;
    }

    goto update_out_from_spare;

} update_out_from_spare: {  //////////////////////////////////////////////////

    Move_Cell(OUT, SPARE);  // leaves SPARE as fresh...good for next step

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;

    assert(STATE == ST_ALL_EVAL_STEP);
    Restart_Evaluator_Level(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} reached_end: {  ////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);

    if (Get_Level_Flag(LEVEL, ALL_VOIDS))
        return VOID;

    return BRANCHED(OUT);
}}


//
//  any: native [
//
//  "Short-circuiting version of OR, using a block of expressions as input"
//
//      return: "First passing evaluative result, or null if none pass"
//          [any-value?]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! the-block!]
//      /predicate "Test for whether an evaluation passes (default is DID)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(any)
//
// 1. Don't let ANY return something falsey, but using heavy form means that
//    it can work with DID/THEN
//
// 2. See ALL[2]
//
// 3. See ALL[3]
{
    INCLUDE_PARAMS_OF_ANY;

    Value* predicate = ARG(predicate);
    Value* block = ARG(block);

    Value* condition;  // could point to OUT or SPARE

    enum {
        ST_ANY_INITIAL_ENTRY = STATE_0,
        ST_ANY_EVAL_STEP,
        ST_ANY_PREDICATE
    };

    switch (STATE) {
      case ST_ANY_INITIAL_ENTRY: goto initial_entry;
      case ST_ANY_EVAL_STEP: goto eval_step_result_in_out;
      case ST_ANY_PREDICATE: goto predicate_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Cell_Series_Len_At(block) == 0)
        return VOID;

    assert(Not_Level_Flag(LEVEL, ALL_VOIDS));
    Set_Level_Flag(LEVEL, ALL_VOIDS);

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;

    if (Is_The_Block(block))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;

    Level* sub = Make_Level_At(block, flags);
    Push_Level(OUT, sub);

    STATE = ST_ANY_EVAL_STEP;
    return CONTINUE_SUBLEVEL(sub);

} eval_step_result_in_out: {  ////////////////////////////////////////////////

    if (Is_Elision(OUT)) {  // (comment "hi")
      handle_elision:
        if (Is_Level_At_End(SUBLEVEL))
            goto reached_end;

        assert(STATE == ST_ANY_EVAL_STEP);
        Restart_Evaluator_Level(SUBLEVEL);
        return CONTINUE_SUBLEVEL(SUBLEVEL);
    }

    Decay_If_Unstable(OUT);
    if (Is_Void(OUT))
        goto handle_elision;  // (if false [<a>])

    Clear_Level_Flag(LEVEL, ALL_VOIDS);

    if (not Is_Nulled(predicate)) {
        SUBLEVEL->executor = &Just_Use_Out_Executor;  // tunnel thru [2]

        STATE = ST_ANY_PREDICATE;
        return CONTINUE(SPARE, predicate, OUT);
    }

    condition = stable_OUT;
    goto process_condition;

} predicate_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Void(SPARE))  // !!! Should void predicate results signal opt-out?
        fail (Error_Bad_Void());

    Isotopify_If_Falsey(OUT);  // predicates can approve "falseys" [3]

    SUBLEVEL->executor = &Evaluator_Executor;  // done tunneling [2]
    STATE = ST_ANY_EVAL_STEP;

    condition = stable_SPARE;
    goto process_condition;

} process_condition: {  //////////////////////////////////////////////////////

    if (Is_Truthy(condition))
        goto return_out;

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;

    assert(STATE == ST_ANY_EVAL_STEP);
    Restart_Evaluator_Level(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} return_out: {  /////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    return BRANCHED(OUT);  // successful ANY returns the value

} reached_end: {  ////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);

    if (Get_Level_Flag(LEVEL, ALL_VOIDS))
        return VOID;

    return nullptr;  // reached end of input and found nothing to return
}}


//
//  case: native [
//
//  "Evaluates each condition, and when true, evaluates what follows it"
//
//      return: "Last matched case evaluation, or null if no cases matched"
//          [any-value?]
//      cases "Conditions followed by branches"
//          [block!]
//      /all "Do not stop after finding first logically true case"
//      /predicate "Unary case-processing action (default is DID)"
//          [<unrun> frame!]
//      <local> discarded branch
//  ]
//
DECLARE_NATIVE(case)
//
// 1. It may seem tempting to run PREDICATE from on `f` directly, allowing it
//    to take arity > 2.  Don't do this.  We have to get a true/false answer
//    *and* know what the right hand argument was, for fallout to work.
//
// 2. Expressions that are between branches are allowed to vaporize via NIHIL
//    (e.g. ELIDE), but voids are not skipped...it would create problems:
//
//        >> condition: false
//        >> case [if condition [<a>] [print "Whoops?"] [<hmm>]]
//        Whoops?
//        == <hmm>
//
// 3. Maintain symmetry with IF on non-taken branches:
//
//        >> if false <some-tag>
//        ** Script Error: if does not allow tag! for its branch...
//
// 4. Last evaluation will "fall out" if there is no branch:
//
//        >> case [false [<a>] false [<b>]]
//        ; void
//
//        >> case [false [<a>] false [<b>] 10 + 20]
//        == 30
//
//    It's a little bit like a quick-and-dirty ELSE (or /DEFAULT), however
//    when you use CASE/ALL it's what is returned even if there's a match:
//
//        >> case/all [1 < 2 [<a>] 3 < 4 [<b>]]
//        == <b>
//
//        >> case/all [1 < 2 [<a>] 3 < 4 [<b>] 10 + 20]
//        == 30  ; so not the same as an ELSE, it's just "fallout"
//
//    This counts as a "branch taken", so void and null are boxed into an
//    antiform pack.
{
    INCLUDE_PARAMS_OF_CASE;

    Value* cases = ARG(cases);
    Value* predicate = ARG(predicate);

    Atom* discarded = LOCAL(discarded);  // slot to write unused results to

    enum {
        ST_CASE_INITIAL_ENTRY = STATE_0,
        ST_CASE_CONDITION_EVAL_STEP,
        ST_CASE_RUNNING_PREDICATE,
        ST_CASE_DISCARDING_GET_GROUP,
        ST_CASE_RUNNING_BRANCH
    };

    switch (STATE) {
      case ST_CASE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_CASE_CONDITION_EVAL_STEP :
        goto condition_result_in_spare;

      case ST_CASE_RUNNING_PREDICATE :
        goto predicate_result_in_spare;

      case ST_CASE_DISCARDING_GET_GROUP :
        goto check_discarded_product_was_branch;

      case ST_CASE_RUNNING_BRANCH :
        goto branch_result_in_out;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Level* L = Make_Level_At(
        cases,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    );

    Push_Level(SPARE, L);

    assert(Is_Fresh(OUT));  // out starts as fresh
    assert(Is_Fresh(SPARE));  // spare starts out as fresh

} handle_next_clause: {  /////////////////////////////////////////////////////

    FRESHEN(SPARE);  // must do before goto reached_end

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;

    STATE = ST_CASE_CONDITION_EVAL_STEP;
    SUBLEVEL->executor = &Evaluator_Executor;
    Restart_Evaluator_Level(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);  // one step to pass predicate [1]

} condition_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Elision(SPARE))  // skip nihils, e.g. ELIDE [2]
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

    if (Is_Void(SPARE))  // error on void predicate results (not same as [2])
        fail (Error_Bad_Void());

    goto processed_result_in_spare;

} processed_result_in_spare: {  //////////////////////////////////////////////

    bool matched = Is_Truthy(stable_SPARE);

    const Element* branch = Copy_Cell(ARG(branch), At_Level(SUBLEVEL));
    Fetch_Next_In_Feed(SUBLEVEL->feed);

    if (not matched) {
        if (not Is_Get_Group(branch))
            goto handle_next_clause;
        else {
            // GET-GROUP! run even on no-match (see IF), but result discarded
        }

        Init_Void(PUSH());  // primed result
        Level* sub = Make_Level_At_Core(
            branch,  // turning into feed drops cell type, :(...) not special
            Level_Specifier(SUBLEVEL),
            LEVEL_MASK_NONE
        );
        sub->executor = &Stepper_Executor;

        STATE = ST_CASE_DISCARDING_GET_GROUP;
        SUBLEVEL->executor = &Just_Use_Out_Executor;
        Push_Level(discarded, sub);
        return CONTINUE_SUBLEVEL(sub);
    }

    STATE = ST_CASE_RUNNING_BRANCH;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    return CONTINUE_CORE(
        OUT,
        LEVEL_FLAG_BRANCH,
        Level_Specifier(SUBLEVEL), branch,
        SPARE
    );

} check_discarded_product_was_branch: {  /////////////////////////////////////

    if (not Any_Branch(discarded))
        fail (Error_Bad_Value_Raw(discarded));  // like IF [3]

    goto handle_next_clause;

} branch_result_in_out: {  ///////////////////////////////////////////////////

    if (not REF(all)) {
        Drop_Level(SUBLEVEL);
        return BRANCHED(OUT);
    }

    goto handle_next_clause;

} reached_end: {  ////////////////////////////////////////////////////////////

    assert(REF(all) or Is_Fresh(OUT));  // never ran a branch, or running /ALL

    Drop_Level(SUBLEVEL);

    if (not Is_Fresh(SPARE)) {  // prioritize fallout result [4]
        Move_Cell(OUT, SPARE);
        return BRANCHED(OUT);
    }

    if (Is_Fresh(OUT))  // none of the clauses of an /ALL ran a branch
        return Init_Void(OUT);

    return BRANCHED(OUT);
}}


//
//  switch: native [
//
//  "Selects a choice and evaluates the block that follows it"
//
//      return: "Last case evaluation, or void if no cases matched"
//          [any-value?]
//      value [any-value?]
//      cases "Block of cases (comparison lists followed by block branches)"
//          [block!]
//      /all "Evaluate all matches (not just first one)"
//      /type "Match based on type constraints, not equality"
//      /predicate "Binary switch-processing action (default is EQUAL?)"
//          [<unrun> frame!]
//      <local> scratch
//  ]
//
DECLARE_NATIVE(switch)
//
// 1. With switch, we have one fixed value ("left") and then an evaluated
//    value from the block ("right") which we pass to a comparison predicate
//    to determine a match.  It may seem tempting to build a frame for the
//    predicate partially specialized with the left, and allow it to consume
//    the right from the feed...allowing it to take arity > 2 (as well as
//    to honor any quoting convention the predicate has.
//
//    Right now that's not what we do, because it would preclude being able
//    to have "fallout".  This should probably be reconsidered, but there are
//    some other SWITCH redesign questions up in the air already:
//
//    https://forum.rebol.info/t/match-in-rust-vs-switch/1835
//
// 2. At one point it was allowed to corrupt the value during comparison, due
//    to the idea equality was transitive.  So if it changes 0.01 to 1% in
//    order to compare it, anything 0.01 would have compared equal to so
//    will 1%.  (Would be required for `a = b` and `b = c` to properly imply
//    `a = c`.)
//
//    ...HOWEVER... this mutated the branch fallout, and quote removals were
//    distorting comparisons.  So it copies the cell into a scratch location.
//
// 3. Fallout is used in situations like:
//
//        lib: switch config.platform [
//            'Windows [%windows.lib]
//            'Linux [%linux.a]
//            null
//        ]
//
//    These cases still count as "branch taken", so the null is put in a
//    PACK!.
{
    INCLUDE_PARAMS_OF_SWITCH;

    Value* left = ARG(value);
    Value* predicate = ARG(predicate);
    Value* cases = ARG(cases);

    Atom* scratch = LOCAL(scratch);

    enum {
        ST_SWITCH_INITIAL_ENTRY = STATE_0,
        ST_SWITCH_EVALUATING_RIGHT,
        ST_SWITCH_RUNNING_BRANCH
    };

    switch (STATE) {
      case ST_SWITCH_INITIAL_ENTRY:
        goto initial_entry;

      case ST_SWITCH_EVALUATING_RIGHT:
        goto right_result_in_spare;

      case ST_SWITCH_RUNNING_BRANCH:
        if (not REF(all)) {
            Drop_Level(SUBLEVEL);
            return BRANCHED(OUT);
        }
        goto next_switch_step;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    assert(Is_Fresh(SPARE));  // initial condition
    assert(Is_Fresh(OUT));  // if no writes to out performed, we act void

    if (REF(type) and REF(predicate))
        fail (Error_Bad_Refines_Raw());

    Level* sub = Make_Level_At(
        cases,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    );

    Push_Level(SPARE, sub);

} next_switch_step: {  ///////////////////////////////////////////////////////

    FRESHEN(SPARE);  // fallout must be reset each time

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;

    const Element* at = At_Level(SUBLEVEL);

    if (Is_Block(at) or Is_Frame(at)) {  // seen with no match in effect
        Fetch_Next_In_Feed(SUBLEVEL->feed);  // just skip over it
        goto next_switch_step;
    }

    STATE = ST_SWITCH_EVALUATING_RIGHT;
    SUBLEVEL->executor = &Evaluator_Executor;
    Restart_Evaluator_Level(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);  // no direct predicate call [1]

} right_result_in_spare: {  //////////////////////////////////////////////////

    if (Is_Elision(SPARE))  // skip comments or ELIDEs
        goto next_switch_step;  // see note [2] in comments for CASE

    if (Is_Level_At_End(SUBLEVEL))
        goto reached_end;  // nothing left, so drop frame and return

    if (REF(type)) {
        Decay_If_Unstable(SPARE);

        if (not Any_Type_Value(SPARE))
            fail ("SWITCH/TYPE requires comparisons to TYPE-XXX!");

        if (not Typecheck_Atom(stable_SPARE, left))
            goto next_switch_step;
    }
    else if (Is_Nulled(predicate)) {
        Decay_If_Unstable(SPARE);

        const bool strict = false;
        Copy_Cell(scratch, left);
        if (0 != Compare_Modify_Values(scratch, SPARE, strict))
            goto next_switch_step;
    }
    else {
        // `switch x .greater? [10 [...]]` acts like `case [x > 10 [...]]
        // The ARG(value) passed in is the left/first argument to compare.
        //
        if (rebRunThrows(
            cast(Value*, scratch),  // <-- output cell
            predicate,
                rebQ(left),  // first arg (left hand side if infix)
                rebQ(SPARE)  // second arg (right hand side if infix)
        )){
            return BOUNCE_THROWN;  // aborts sublevel
        }
        if (Is_Falsey(Stable_Unchecked(scratch)))
            goto next_switch_step;
    }

    const Element* at = Try_At_Level(SUBLEVEL);

    while (true) {  // skip ahead for BLOCK!/ACTION! to process the match
        if (at == nullptr)
            goto reached_end;

        if (Is_Block(at) or Is_Meta_Block(at) or Is_Frame(at))
            break;

        Fetch_Next_In_Feed(SUBLEVEL->feed);
        at = At_Level(SUBLEVEL);
    }

    STATE = ST_SWITCH_RUNNING_BRANCH;
    SUBLEVEL->executor = &Just_Use_Out_Executor;
    return CONTINUE_CORE(
        FRESHEN(OUT),
        LEVEL_FLAG_BRANCH,
        Level_Specifier(SUBLEVEL), at
    );

} reached_end: {  ////////////////////////////////////////////////////////////

    assert(REF(all) or Is_Fresh(OUT));

    Drop_Level(SUBLEVEL);

    if (not Is_Fresh(SPARE)) {  // see remarks in CASE on fallout prioritization
        Move_Cell(OUT, SPARE);
        return BRANCHED(OUT);
    }

    if (Is_Fresh(OUT))  // no fallout, and no branches ran
        return Init_Void(OUT);

    return BRANCHED(OUT);
}}


//
//  default: enfix native [
//
//  "Set word or path to a default value if it is not set yet"
//
//      return: "Former value or branch result, can only be null if no target"
//          [any-value?]
//      :target "Word or path which might be set appropriately (or not)"
//          [set-group! set-word! set-tuple!]  ; to left of DEFAULT
//      :branch "If target needs default, this is evaluated and stored there"
//          [any-branch?]
//      /predicate "Test for what's considered empty (default is null + void)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(default)
//
// 1. The TARGET may be something like a TUPLE! that contains GROUP!s.  This
//    could put us at risk of double-evaluation if we do a GET to check the
//    variable--find it's unset--and then use that tuple again.  GET and SET
//    have an answer for this problem in the form of giving back a block of
//    `steps` which can resolve the variable without doing more evaluations.
//
// 2. Usually we only want to consider variables with states that are known
//    to mean "emptiness" as candidates for overriding.
{
    INCLUDE_PARAMS_OF_DEFAULT;

    Value* target = ARG(target);
    Value* branch = ARG(branch);
    Value* predicate = ARG(predicate);

    Value* steps = ARG(return);  // reuse slot to save resolved steps [1]

    enum {
        ST_DEFAULT_INITIAL_ENTRY = STATE_0,
        ST_DEFAULT_GETTING_TARGET,
        ST_DEFAULT_RUNNING_PREDICATE,
        ST_DEFAULT_EVALUATING_BRANCH
    };

    switch (STATE) {
      case ST_DEFAULT_INITIAL_ENTRY: goto initial_entry;
      case ST_DEFAULT_GETTING_TARGET: assert(false); break;  // !!! TBD
      case ST_DEFAULT_RUNNING_PREDICATE: goto predicate_result_in_spare;
      case ST_DEFAULT_EVALUATING_BRANCH: goto branch_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Get_Var_Core_Throws(OUT, steps, target, SPECIFIED))  // [1]
        return THROWN;

    if (not Is_Nulled(predicate)) {
        STATE = ST_DEFAULT_RUNNING_PREDICATE;
        return CONTINUE(SPARE, predicate, OUT);
    }

    if (not Is_Trash(OUT) and not Is_Nulled(OUT))
        return OUT;  // consider it a "value" [2]

    STATE = ST_DEFAULT_EVALUATING_BRANCH;
    return CONTINUE(SPARE, branch, OUT);

} predicate_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Truthy(stable_SPARE))
        return OUT;

    STATE = ST_DEFAULT_EVALUATING_BRANCH;
    return CONTINUE(SPARE, branch, OUT);

} branch_result_in_spare: {  /////////////////////////////////////////////////

    if (Set_Var_Core_Throws(OUT, nullptr, steps, SPECIFIED, stable_SPARE)) {
        assert(false);  // shouldn't be able to happen.
        fail (Error_No_Catch_For_Throw(LEVEL));
    }

    return COPY(SPARE);
}}


//
//  catch: native [
//
//  "Catches a throw from a block and returns its value"
//
//      return: "Thrown value"
//          [any-value?]
//      block "Block to evaluate"
//          [block!]
//      /name "Catches a named throw (single name if not block)"
//          [block! word! frame! object!]
//      /quit "Special catch for QUIT native"
//      /any "Catch all throws except QUIT (can be used with /QUIT)"
//  ]
//
DECLARE_NATIVE(catch)
//
// There's a refinement for catching quits, and CATCH/ANY will not alone catch
// it (you have to CATCH/ANY/QUIT).  Currently the label for quitting is the
// NATIVE! function value for QUIT.
{
    INCLUDE_PARAMS_OF_CATCH;

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

    // /ANY would override /NAME, so point out the potential confusion
    //
    if (REF(any) and REF(name))
        fail (Error_Bad_Refines_Raw());

    STATE = ST_CATCH_RUNNING_CODE;
    return CATCH_CONTINUE_BRANCH(OUT, ARG(block));

} code_result_in_out: {  //////////////////////////////////////////////////////

    if (not THROWING)
        return nullptr;  // no throw means just return null (pure, for ELSE)

    const Value* label = VAL_THROWN_LABEL(LEVEL);

    if (REF(any) and not (
        Is_Frame(label)
        and ACT_DISPATCHER(VAL_ACTION(label)) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(quit) and (
        Is_Frame(label)
        and ACT_DISPATCHER(VAL_ACTION(label)) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(name)) {
        //
        // We use equal? by way of Compare_Modify_Values, and re-use the
        // refinement slots for the mutable space

        Value* temp1 = ARG(quit);
        Value* temp2 = ARG(any);

        if (Is_Block(ARG(name))) {
            //
            // Test all the words in the block for a match to catch

            const Element* tail;
            const Element* candidate = Cell_Array_At(&tail, ARG(name));
            for (; candidate != tail; candidate++) {
                //
                // !!! Should we test a typeset for illegal name types?
                //
                if (Is_Block(candidate))
                    fail (PARAM(name));

                Derelativize(temp1, candidate, Cell_Specifier(ARG(name)));
                Copy_Cell(temp2, label);

                // Return the THROW/NAME's arg if the names match
                //
                bool strict = false;  // e.g. EQUAL?, better if STRICT-EQUAL?
                if (0 == Compare_Modify_Values(temp1, temp2, strict))
                    goto was_caught;
            }
        }
        else {
            Copy_Cell(temp1, ARG(name));
            Copy_Cell(temp2, label);

            // Return the THROW/NAME's arg if the names match
            //
            bool strict = false;  // e.g. EQUAL?, better if STRICT-EQUAL?
            if (0 == Compare_Modify_Values(temp1, temp2, strict))
                goto was_caught;
        }
    }
    else {
        // Return THROW's arg only if it did not have a /NAME supplied
        //
        if (Is_Nulled(label) and (REF(any) or not REF(quit)))
            goto was_caught;
    }

    return THROWN; // throw label and value are held task local

  was_caught:  ///////////////////////////////////////////////////////////////

    CATCH_THROWN(OUT, level_); // thrown value

    return BRANCHED(OUT);  // a caught NULL triggers THEN, not ELSE
}}


//
//  throw: native [
//
//  "Throws control back to a previous catch"
//
//      return: []  ; !!! notation for divergent function?
//      ^value "Value returned from catch"
//          [any-atom?]
//      /name "Throws to a named catch"
//          [word! frame! object!]
//  ]
//
DECLARE_NATIVE(throw)
//
// Choices are currently limited for what one can use as a "name" of a THROW.
// Note blocks as names would conflict with the `name_list` feature in CATCH.
//
// !!! Should it be /NAMED instead of /NAME?
{
    INCLUDE_PARAMS_OF_THROW;

    Atom* atom = Copy_Cell(SPARE, ARG(value));

    if (Is_Meta_Of_Void(atom))
        Init_Heavy_Void(atom);
    else if (Is_Meta_Of_Null(atom))
        Init_Heavy_Null(atom);
    else
        Meta_Unquotify_Undecayed(atom);

    return Init_Thrown_With_Label(LEVEL, atom, ARG(name));  // nulled name ok
}


//
//  Debranch_Output: C
//
// When a branch runs, it specifically wants to remove signals that are used
// by THEN and ELSE.  This means NULL and VOID can't stay as those states:
//
//     >> if true [null] else [print "Otherwise you'd get this :-("]
//     Otherwise you'd get this :-(  ; <-- BAD!
//
// See notes about "Heavy Null" and "Heavy Void" for how the variations carry
// some behaviors of the types, while not being technically void or null.
//
void Debranch_Output(Atom* out) {
    if (Not_Stable(out)) {
        if (Is_Lazy(out)) {
            //
            // We don't have to fully reify the object, we just need to make sure
            // its THEN and ELSE fields are unset.
            //
            const Symbol* syms[2] = {Canon(ELSE), Canon(THEN)};
            int i;
            for (i = 0; i < 2; ++i) {
                Option(Value*) hook = Select_Symbol_In_Context(out, syms[i]);
                if (hook)
                    Init_Void(unwrap(hook));
            }
        }
    }
    else if (Is_Void(out))
        Init_Heavy_Void(out);
    else if (Is_Nulled(out))
        Init_Heavy_Null(out);
}


//
//  Pushed_Decaying_Level: C
//
bool Pushed_Decaying_Level(Atom* out, const Atom* obj, Flags flags) {
    if (out != obj)
        Copy_Cell(out, obj);
    QUOTE_BYTE(out) = NOQUOTE_1;
    Option(Value*) decayer = Select_Symbol_In_Context(
        cast(const Cell*, out),
        Canon(DECAY)
    );
    if (not decayer)
        fail ("Asked to decay lazy object with no DECAY method");

    bool pushed = Pushed_Continuation(
        out,
        flags,
        SPECIFIED,
        unwrap(decayer),
        nullptr  // no arguments to decay--must answer in isolation
    );
    if (not pushed)
        return false;

    return true;
}
