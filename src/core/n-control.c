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
//    See %sys-void.h for more information about this mechanic.)
//
// * If a branch *does* run--and its evaluation happens to produce VOID or
//   NULL, then special isotopes are returned: either a zero-lenght GROUP!
//   isotope for void, or a BLANK! isotope for NULL.  This way THEN runs
//   instead of ELSE.  Although this does mean there is some conflation of
//   the results, the conflated values have properties that mostly align with
//   what their intent was--so it works about as well as it can.
//
//   (See %sys-bad-word.h for more details about isotopes.)
//
// * Zero-arity function values used as branches will be executed, and
//   single-arity functions used as branches will also be executed--but passed
//   the value of the triggering condition.  Especially useful with lambda:
//
//       >> if 1 < 2 [10 + 20] then x -> [print ["THEN got" x]]
//       THEN got 30
//
//   Isotopes of NULL, FALSE, and BLANK are decayed before being passed to the
//   function, unless the argument is taken as a ^META parameter.
//
//   (See Do_Branch_Throws() for supported ANY-BRANCH! types and behaviors.)
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
// push a frame with the branch executor which can complete and then run the
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
//    OUT cell as `with` the FRAME_FLAG_BRANCH was taken off at the callsite.
//
Bounce Group_Branch_Executor(Frame(*) frame_)
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

    Frame(*) evaluator = Make_Frame(
        FRAME->feed,
        ((FRAME->flags.bits & (~ FLAG_STATE_BYTE(255)))  // take out state 1
            | FRAME_FLAG_BRANCH)
    );
    Push_Frame(SPARE, evaluator);
    evaluator->executor = &Array_Executor;

    Clear_Frame_Flag(FRAME, ALLOCATED_FEED);
    FRAME->feed = TG_End_Feed;  // feed consumed by subframe

    STATE = ST_GROUP_BRANCH_RUNNING_GROUP;
    return CATCH_CONTINUE_SUBFRAME(evaluator);

} group_result_in_spare: {  //////////////////////////////////////////////////

    if (ANY_GROUP(SPARE))
        fail (Error_Bad_Branch_Type_Raw());  // stop infinite recursion (good?)

    Value(const*) with = Is_Fresh(OUT) ? nullptr : OUT;  // with here, see [1]

    assert(Is_Frame_At_End(FRAME));
    return DELEGATE_BRANCH(OUT, SPARE, with);  // couldn't do (OUT, OUT, SPARE)
}}


//
//  if: native [
//
//  {When TO LOGIC! CONDITION is true, execute branch}
//
//      return: "void if branch not run, otherwise branch result"
//          [<opt> <void> any-value! logic!]
//      condition [<opt> any-value! logic!]
//      :branch "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch!]
//  ]
//
DECLARE_NATIVE(if)
//
// 1. It's a common mistake to write something like `if [1 > 2]` and be
//    surprised that is considered "truthy" (as it's an unevaluated block).
//    So Is_Conditional_False() notices a hidden bit on ARG(condition) that
//    tells IF whether a BLOCK! argument is the product of an evaluation.
//    (See CELL_FLAG_UNEVALUATED for more information on this feature.)
//
// 2. Evaluations must be performed through continuations, so IF can't be on
//    the C function stack while the branch runs.  Rather than asking to be
//    called back after the evaluation so it can turn null into null isotopes
//    and voids into none, it requests "branch semantics" so that the evaluator
//    does that automatically.  `delegate` means it doesn't need a callback.
{
    INCLUDE_PARAMS_OF_IF;

    Value(*) condition = ARG(condition);
    Value(*) branch = ARG(branch);

    if (Is_Conditional_False(condition))  // errors on literal block, see [1]
        return VOID;

    return DELEGATE_BRANCH(OUT, branch, condition);  // no callback, see [2]
}


//
//  either: native [
//
//  {Choose a branch to execute, based on TO-LOGIC of the CONDITION value}
//
//      return: [<opt> any-value!]
//          "Returns null if either branch returns null (unlike IF...ELSE)"
//      condition [<opt> any-value! logic!]
//      :true-branch "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch!]
//      :false-branch
//          [any-branch!]
//  ]
//
DECLARE_NATIVE(either)
{
    INCLUDE_PARAMS_OF_EITHER;

    Value(*) condition = ARG(condition);

    Value(*) branch = Is_Conditional_True(condition)  // see [1] on IF native
        ? ARG(true_branch)
        : ARG(false_branch);

    return DELEGATE_BRANCH(OUT, branch, condition);  // see [2] on IF native
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
// 1. The input is a ^META parameter in order to react to voids and tolerate
//    isotopes.  But we don't want to actually return a quoted version of the
//    input if branches don't run, so unmeta them.
//
// 2. THEN and ELSE want to pass on contents of a multi-return pack to
//    branches if applicable.  But the decision on whether it's a THEN or
//    ELSE case comes from the first parameter in the pack.
//
// 3. With the exception of ~[_]~ and ~[~]~ when /DECAY is used, a "pack"
//    (isotopic block) will always run a THEN and not an ELSE.  If a function
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
    Frame(*) frame_,
    bool then  // true when doing a THEN
){
    INCLUDE_PARAMS_OF_THEN;  // assume frame compatibility w/ELSE

    Value(*) in = ARG(optional);
    Value(*) branch = ARG(branch);

    Meta_Unquotify(in);  // see [1]

    if (Is_Raised(in)) {  // definitional failure, skip
        STATE = ST_THENABLE_REJECTING_INPUT;
        return Copy_Cell(OUT, in);
    }

    if (not Is_Lazy(in))  // Packs run THEN, including ~[_]~ and ~[~]~, see [3]
        goto test_not_lazy;

    goto handle_lazy_object;

  handle_lazy_object: {  /////////////////////////////////////////////////////

    option(Value(*)) then_hook = Select_Symbol_In_Context(in, Canon(THEN));
    if (then_hook and Is_Void(unwrap(then_hook)))
        then_hook = nullptr;  // can be unset by Debranch_Output()

    option(Value(*)) else_hook = Select_Symbol_In_Context(in, Canon(ELSE));
    if (else_hook and Is_Void(unwrap(else_hook)))
        else_hook = nullptr;  // can be unset by Debranch_Output()

    if (not then_hook and not else_hook) {  // !!! should it always take THEN?
        if (not Pushed_Reifying_Frame(  // fails if no reify method, see [4]
            SPARE,
            in,
            FRAME_FLAG_META_RESULT
        )){
            Copy_Cell(in, SPARE);  // cheap reification... (e.g. quoted)
            Meta_Unquotify(in);  // see [1]
            assert(STATE == ST_THENABLE_INITIAL_ENTRY);
            assert(not Is_Isotope(in));
            goto test_not_lazy;
        }

        STATE = ST_THENABLE_REIFYING_SPARE;  // will call helper again
        return CONTINUE_SUBFRAME(TOP_FRAME);
    }

    Value(*) hook;
    if (then) {
        if (not then_hook) {
            STATE = ST_THENABLE_REJECTING_INPUT;
            return Copy_Cell(OUT, in);  // pass lazy object thru to ELSEs
        }
        hook = unwrap(then_hook);
    }
    else {
        if (not else_hook) {
            STATE = ST_THENABLE_REJECTING_INPUT;
            return Copy_Cell(OUT, in);  // pass lazy object thru to THENs (?)
        }
        hook = unwrap(else_hook);
    }

    STATE = ST_THENABLE_RUNNING_BRANCH;

    if (then_hook and else_hook)  // THEN is likely passthru if both
        return DELEGATE(OUT, hook, branch);  // not DELEGATE_BRANCH, see [5]

    if (not IS_ACTION(hook))  // if not full control, assume must use BRANCH
        fail ("non-ACTION! found in THEN or ELSE method of lazy object");

    return DELEGATE_BRANCH(OUT, hook, branch);  // BRANCH for safety, see [6]

} test_not_lazy: {  //////////////////////////////////////////////////////////

    assert(not Is_Lazy(in));

    if (Is_Void(in) or (REF(decay) and Is_Heavy_Void(in))) {
        if (then) {
            STATE = ST_THENABLE_REJECTING_INPUT;
            if (Is_Pack(in))
                return Copy_Cell(OUT, in);
            return VOID;  // then of void, don't want to write to output cell
        }
        STATE = ST_THENABLE_RUNNING_BRANCH;
        return DELEGATE_BRANCH(OUT, branch, in);  // else of void
    }

    if (Is_Nulled(in) or (REF(decay) and Is_Heavy_Null(in))) {
        if (then) {
            STATE = ST_THENABLE_REJECTING_INPUT;
            return Copy_Cell(OUT, in);  // then of null, may be pack
        }
        STATE = ST_THENABLE_RUNNING_BRANCH;
        return DELEGATE_BRANCH(OUT, branch, in);  // else of null
    }

    if (not then) {
        STATE = ST_THENABLE_REJECTING_INPUT;
        return Copy_Cell(OUT, in);  // passthru, see [4]
    }
    STATE = ST_THENABLE_RUNNING_BRANCH;
    return DELEGATE_BRANCH(OUT, branch, in);  // then branch, takes arg
}}


//
//  did: native [
//
//  {Synonym for NOT NULL? that is isotope tolerant (IF DID is prefix THEN)}
//
//      return: [logic!]
//      ^optional "Argument to test"
//          [<opt> <void> <pack> any-value!]
//      /decay "Pre-decay ~_~ isotope input to NULL"
//      <local> branch  ; for frame compatibility with THEN/ELSE/ALSO
//  ]
//
DECLARE_NATIVE(did_1)  // see TO-C-NAME for why the "_1" is needed
//
// DID exists as a complement to isotopes to help solve conflation of falsey
// values with conditional tests.  One example:
//
//     >> match [logic! integer!] false
//     == ~false~  ; isotope
//
//     >> if (match [logic! integer!] false) [print "Want this to run"]
//     ** Error: We save you by not letting isotopes be conditionally tested
//
//     >> did match [logic! integer!] false
//     == #[true]  ; DID tolerates isotopes, returns #[false] only on true NULL
//
//     >> if (did match [logic! integer!] false) [print "Praise isotopes!"]
//     Praise isotopes!
//
// By making routines that intend to return ANY-VALUE! (even falsey ones) on
// success return the falsey ones as isotopes, incorrect uses can be caught
// and guided to use DID or DIDN'T (or whatever they actually meant).
{
    INCLUDE_PARAMS_OF_DID_1;

    Value(*) in = ARG(optional);
    USED(ARG(decay));  // used by helper
    USED(ARG(branch));

    switch (STATE) {
      case ST_THENABLE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_THENABLE_REIFYING_SPARE :
        Copy_Cell(in, SPARE);
        goto reifying_input;  // multiple reifications may be needed

      case ST_THENABLE_RUNNING_BRANCH :
        goto return_true;

      default :
        assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Quotify(Quasify(Init_Word(ARG(branch), Canon(DID_1))), 1);  // see [1]

    goto reifying_input;

} reifying_input: {  /////////////////////////////////////////////////////////

    bool then = true;
    Bounce bounce = Then_Else_Isotopic_Object_Helper(frame_, then);

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

    return Init_True(OUT);  // can't trust branch product, see [1]
}}


//
//  didn't: native [
//
//  {Synonym for NULL? that is isotope tolerant (IF DIDN'T is prefix ELSE)}
//
//      return: [logic!]
//      ^optional "Argument to test"
//          [<opt> <void> any-value!]
//      /decay "Pre-decay ~_~ isotope input to NULL"
//      <local> branch  ; for frame compatibility with THEN/ELSE/ALSO
//  ]
//
DECLARE_NATIVE(didnt)
{
    INCLUDE_PARAMS_OF_DIDNT;

    Value(*) in = ARG(optional);
    USED(ARG(decay));  // used by helper
    USED(ARG(branch));

    if (Is_Meta_Of_Void(in) or Is_Meta_Of_Null(in))
        return Init_True(OUT);

    if (REF(decay) and Is_Quasi_Blank(in))
        return Init_True(OUT);

    return Init_False(OUT);
}


//
//  then: enfix native [
//
//  {If input is null, return null, otherwise evaluate the branch}
//
//      return: "null if input is null, or branch result"
//          [<opt> <void> any-value!]
//      ^optional "<deferred argument> Run branch if this is not null"
//          [<opt> <void> <fail> <pack> any-value!]
//      /decay "Pre-decay ~_~ isotope input to NULL"
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [any-branch! ~action!~]
//  ]
//
DECLARE_NATIVE(then)  // see `tweak :then 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_THEN;

    Value(*) in = ARG(optional);
    Decay_If_Activation(ARG(branch));
    USED(ARG(branch));  // used by helper
    USED(ARG(decay));

    switch (STATE) {
      case ST_THENABLE_INITIAL_ENTRY :
        goto reifying_input;

      case ST_THENABLE_REIFYING_SPARE :
        Copy_Cell(in, SPARE);
        goto reifying_input;

      default : assert(false);
    }

  reifying_input: {  /////////////////////////////////////////////////////////

    bool then = true;
    Bounce bounce = Then_Else_Isotopic_Object_Helper(frame_, then);
    return bounce;
}}


//
//  else: enfix native [
//
//  {If input is not null, return that value, otherwise evaluate the branch}
//
//      return: "Input value if not null, or branch result"
//          [<opt> <void> any-value!]
//      ^optional "<deferred argument> Run branch if this is null"
//          [<opt> <void> <fail> <pack> any-value!]
//      /decay "Pre-decay ~_~ isotope input to NULL"
//      :branch [any-branch! ~action!~]
//  ]
//
DECLARE_NATIVE(else)  // see `tweak :else 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ELSE;

    Value(*) in = ARG(optional);
    Decay_If_Activation(ARG(branch));
    USED(ARG(branch));  // used by helper
    USED(ARG(decay));

    switch (STATE) {
      case ST_THENABLE_INITIAL_ENTRY :
        goto reifying_input;

      case ST_THENABLE_REIFYING_SPARE :
        Copy_Cell(in, SPARE);
        goto reifying_input;

      default : assert(false);
    }

  reifying_input: {  /////////////////////////////////////////////////////////

    bool then = false;
    Bounce bounce = Then_Else_Isotopic_Object_Helper(frame_, then);
    return bounce;
}}


//
//  also: enfix native [
//
//  {For non-null input, evaluate and discard branch (like a pass-thru THEN)}
//
//      return: "The same value as input, regardless of if branch runs"
//          [<opt> <void> any-value!]
//      ^optional "<deferred argument> Run branch if this is not null"
//          [<opt> <void> <fail> any-value!]
//      /decay "Pre-decay ~_~ isotope input to NULL"
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [any-branch! ~action!~]
//  ]
//
DECLARE_NATIVE(also)  // see `tweak :also 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ALSO;  // `then func [x] [(...) :x]` => `also [...]`

    Value(*) in = ARG(optional);
    Value(*) branch = ARG(branch);
    Decay_If_Activation(ARG(branch));

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

    if (Is_Meta_Of_Void(in))
        return VOID;  // telegraph invisible intent

    if (Is_Meta_Of_Null(in))
        return nullptr;  // telegraph pure null

    if (Is_Meta_Of_Raised(in)) {  // definitional failure, skip
        Copy_Cell(OUT, in);
        Unquasify(OUT);
        return Raisify(OUT);
    }

    if (REF(decay) and Is_Meta_Of_Heavy_Null(in))
        return Init_Heavy_Null(OUT);  // telegraph null isotope

    STATE = ST_ALSO_RUNNING_BRANCH;
    return CONTINUE(SPARE, branch, Meta_Unquotify(in));

} return_original_input: {  //////////////////////////////////////////////////

    return COPY(in);  // in argument has already been Meta_Unquotify()'d
}}


//
//  match: native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input if it matched, NULL if it did not (isotope if falsey)"
//          [<opt> any-value! logic!]
//      test "Typeset or arity-1 filter function"
//          [<opt> logic! action! block! datatype! typeset!]
//      value [<maybe> <opt> any-value! logic!]
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

    REBVAL *v = ARG(value);
    REBVAL *test = ARG(test);

    switch (VAL_TYPE(test)) {
      case REB_NULL:
        if (not Is_Nulled(v))
            return nullptr;
        break;

      case REB_LOGIC:
        if (Is_Truthy(v) != VAL_LOGIC(test))
            return nullptr;
        break;

      case REB_DATATYPE:
        if (VAL_TYPE(v) != VAL_TYPE_KIND(test))
            return nullptr;
        break;

      case REB_BLOCK: {
        Bounce r = MAKE_Typeset(frame_, REB_TYPESET, nullptr, test);
        if (r == BOUNCE_THROWN)
            return THROWN;
        test = Move_Cell(SPARE, OUT);;
        goto test_is_typeset; }

      case REB_TYPESET:
      test_is_typeset:
        if (not TYPE_CHECK(test, VAL_TYPE(v)))
            return nullptr;
        break;

      case REB_ACTION: {
        if (rebRunThrows(
            SPARE,  // <-- output cell
            test, rebQ(v)
        )){
            return THROWN;
        }
        if (Is_Falsey(SPARE))
            return nullptr;
        break; }

      default:
        fail (PARAM(test));  // all test types should be accounted for in switch
    }

    //=//// IF IT GOT THIS FAR WITHOUT RETURNING, THE TEST MATCHED /////////=//

    // Falsey matched values return isotopes to show they did match, but to
    // avoid misleading falseness of the result:
    //
    //     >> value: false
    //     >> if match [integer! logic!] value [print "Won't run :-("]
    //     ; null  <-- this would be a bad result!
    //
    // So successful matching of falsey values will give back ~false~,
    // or ~_~ isotopes.  This can be consciously turned back into their
    // original values with DECAY, which happens automatically in assignments.
    //
    //     >> match [<opt>] null
    //     == ~_~  ; isotope
    //
    //     >> decay match [<opt>] null
    //     ; null
    //
    Isotopify_If_Falsey(v);

    Copy_Cell(OUT, v);  // Otherwise, input is the result

    return BRANCHED(OUT);
}


//
//  must: native [
//
//  {Ensure that the argument is not NULL}
//
//      return: "Same as input value if non-NULL"
//          [any-value!]
//      value [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(must)  // `must x` is a faster synonym for `non null x`
{
    INCLUDE_PARAMS_OF_MUST;

    if (Is_Nulled(ARG(value)))
        fail ("MUST requires argument to not be NULL");

    return COPY(ARG(value));
}


//
//  all: native [
//
//  {Short-circuiting variant of AND, using a block of expressions as input}
//
//      return: "Product of last passing evaluation if all truthy, else null"
//          [<opt> <void> any-value!]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! the-block!]
//      /predicate "Test for whether an evaluation passes (default is DID)"
//          [action! ~action!~]
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
//    your only two choices.  Because Ren-C has the option of isotopes, it's
//    better to signal to the caller that nothing happened.  For an example
//    of how useful it is, see the loop wrapper FOR-BOTH.  Other behaviors
//    can be forced with (all [... null]) or (any [true ...])
//
// 2. The predicate-running condition gets pushed over the "keepalive" stepper,
//    but we don't want the stepper to take a step before coming back to us.
//    Temporarily patch out the Evaluator_Executor() so we get control back
//    without that intermediate step.
//
// 3. The only way a falsey evaluation should make it to the end is if a
//    predicate let it pass.  Don't want that to trip up `if all` so make it
//    an isotope...but this way `(all/predicate [null] :not?) then [<runs>]`
{
    INCLUDE_PARAMS_OF_ALL;

    Value(*) block = ARG(block);
    Value(*) predicate = ARG(predicate);

    Value(*) scratch = ARG(scratch);

    Value(*) condition;  // will be found in OUT or scratch

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

    if (VAL_LEN_AT(block) == 0)
        return VOID;

    if (REF(predicate))
        Decay_If_Activation(predicate);

    Finalize_Void(OUT);  // default to void if all conditions vaporize, see [1]

    Flags flags = FRAME_FLAG_TRAMPOLINE_KEEPALIVE;

    if (IS_THE_BLOCK(block))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;

    Frame(*) subframe = Make_Frame_At(block, flags);
    Push_Frame(SPARE, subframe);

    STATE = ST_ALL_EVAL_STEP;
    return CONTINUE_SUBFRAME(subframe);

} eval_step_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Void(SPARE)) {  // void steps, e.g. (comment "hi") (if false [<a>])
        if (Is_Frame_At_End(SUBFRAME))
            goto reached_end;

        assert(STATE == ST_ALL_EVAL_STEP);
        Restart_Evaluator_Frame(SUBFRAME);
        return CONTINUE_SUBFRAME(SUBFRAME);
    }

    Decay_If_Isotope(SPARE);

    if (not Is_Nulled(predicate)) {
        SUBFRAME->executor = &Just_Use_Out_Executor;  // tunnel thru, see [2]

        STATE = ST_ALL_PREDICATE;
        return CONTINUE(scratch, predicate, SPARE);
    }

    condition = SPARE;  // without predicate, `condition` is same as evaluation
    goto process_condition;

} predicate_result_in_scratch: {  ////////////////////////////////////////////

    if (Is_Void(scratch))  // !!! Should void predicate results signal opt-out?
        fail (Error_Bad_Void());

    Isotopify_If_Falsey(SPARE);  // predicates can approve "falseys", see [3]

    SUBFRAME->executor = &Evaluator_Executor;  // done tunneling, see [2]
    STATE = ST_ALL_EVAL_STEP;

    condition = scratch;
    goto process_condition;  // with predicate, `condition` is predicate result

} process_condition: {  //////////////////////////////////////////////////////

    if (Is_Falsey(condition)) {  // errors on most isotopes (but not null)
        Drop_Frame(SUBFRAME);
        return nullptr;
    }

    Move_Cell(OUT, SPARE);  // leaves SPARE as fresh...good for next step

    if (Is_Frame_At_End(SUBFRAME))
        goto reached_end;

    assert(STATE == ST_ALL_EVAL_STEP);
    Restart_Evaluator_Frame(SUBFRAME);
    return CONTINUE_SUBFRAME(SUBFRAME);

} reached_end: {  ////////////////////////////////////////////////////////////

    Drop_Frame(SUBFRAME);

    if (Is_Void(OUT))
        return VOID;

    return BRANCHED(OUT);
}}


//
//  any: native [
//
//  {Short-circuiting version of OR, using a block of expressions as input}
//
//      return: "First passing evaluative result, or null if none pass"
//          [<opt> <void> any-value!]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! the-block!]
//      /predicate "Test for whether an evaluation passes (default is DID)"
//          [action! ~action!~]
//  ]
//
DECLARE_NATIVE(any)
//
// 1. Don't let ANY return something falsey, but using an isotope means that
//    it can work with DID/THEN
//
// 2. See ALL[2]
//
// 3. See ALL[3]
{
    INCLUDE_PARAMS_OF_ANY;

    Value(*) predicate = ARG(predicate);
    Value(*) block = ARG(block);

    Value(*) condition;  // could point to OUT or SPARE

    enum {
        ST_ANY_INITIAL_ENTRY = STATE_0,
        ST_ANY_EVAL_STEP,
        ST_ANY_PREDICATE
    };

    switch (STATE) {
      case ST_ANY_INITIAL_ENTRY: goto initial_entry;
      case ST_ANY_EVAL_STEP: goto eval_step_finished;
      case ST_ANY_PREDICATE: goto predicate_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (VAL_LEN_AT(block) == 0)
        return VOID;

    if (REF(predicate))
        Decay_If_Activation(predicate);

    Flags flags = FRAME_FLAG_TRAMPOLINE_KEEPALIVE;

    if (IS_THE_BLOCK(block))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;

    Frame(*) subframe = Make_Frame_At(block, flags);
    Push_Frame(OUT, subframe);

    STATE = ST_ANY_EVAL_STEP;
    return CONTINUE_SUBFRAME(subframe);

} eval_step_finished: {  /////////////////////////////////////////////////////

    if (Is_Void(OUT)) {  // void steps, e.g. (comment "hi") (if false [<a>])
        if (Is_Frame_At_End(SUBFRAME))
            goto reached_end;

        assert(STATE == ST_ANY_EVAL_STEP);
        Restart_Evaluator_Frame(SUBFRAME);
        return CONTINUE_SUBFRAME(SUBFRAME);
    }

    Decay_If_Isotope(OUT);

    if (not Is_Nulled(predicate)) {
        SUBFRAME->executor = &Just_Use_Out_Executor;  // tunnel thru, see [2]

        STATE = ST_ANY_PREDICATE;
        return CONTINUE(SPARE, predicate, OUT);
    }

    condition = OUT;
    goto process_condition;

} predicate_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Void(SPARE))  // !!! Should void predicate results signal opt-out?
        fail (Error_Bad_Void());

    Isotopify_If_Falsey(OUT);  // predicates can approve "falseys", see [3]

    SUBFRAME->executor = &Evaluator_Executor;  // done tunneling, see [2]
    STATE = ST_ANY_EVAL_STEP;

    condition = SPARE;
    goto process_condition;

} process_condition: {  //////////////////////////////////////////////////////

    if (Is_Truthy(condition)) {  // errors on most isotopes (but not null)
        Drop_Frame(SUBFRAME);
        return BRANCHED(OUT);  // successful ANY returns the value
    }

    if (Is_Frame_At_End(SUBFRAME))
        goto reached_end;

    assert(STATE == ST_ANY_EVAL_STEP);
    Restart_Evaluator_Frame(SUBFRAME);
    return CONTINUE_SUBFRAME(SUBFRAME);

} reached_end: {  ////////////////////////////////////////////////////////////

    Drop_Frame(SUBFRAME);
    return nullptr;  // reached end of input and found nothing to return
}}


//
//  case: native [
//
//  {Evaluates each condition, and when true, evaluates what follows it}
//
//      return: "Last matched case evaluation, or null if no cases matched"
//          [<opt> <void> any-value!]
//      cases "Conditions followed by branches"
//          [block!]
//      /all "Do not stop after finding first logically true case"
//      /predicate "Unary case-processing action (default is DID)"
//          [action! ~action!~]
//      <local> discarded
//  ]
//
DECLARE_NATIVE(case)
//
// 1. It may seem tempting to run PREDICATE from on `f` directly, allowing it
//    to take arity > 2.  Don't do this.  We have to get a true/false answer
//    *and* know what the right hand argument was, for fallout to work.
//
// 2. Expressions that are between branches are allowed to vaporize.  This is
//    powerful, but people should be conscious of what can produce voids and
//    not try to use them as conditions:
//
//        >> condition: false
//        >> case [if condition [<a>] [print "Whoops?"] [<hmm>]]
//        Whoops?
//        == <hmm>
//
//   Those who dislike this can use variations of CASE that require `=>`.
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
{
    INCLUDE_PARAMS_OF_CASE;

    REBVAL *cases = ARG(cases);
    REBVAL *predicate = ARG(predicate);

    REBVAL *discarded = LOCAL(discarded);  // slot to write unused results to

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

    if (REF(predicate))
        Decay_If_Activation(predicate);

    Frame(*) f = Make_Frame_At(
        cases,
        FRAME_FLAG_TRAMPOLINE_KEEPALIVE
    );

    Push_Frame(SPARE, f);

    assert(Is_Fresh(OUT));  // out starts as fresh
    assert(Is_Fresh(SPARE));  // spare starts out as fresh

} handle_next_clause: {  /////////////////////////////////////////////////////

    FRESHEN(SPARE);  // must do before goto reached_end

    if (Is_Frame_At_End(SUBFRAME))
        goto reached_end;

    STATE = ST_CASE_CONDITION_EVAL_STEP;
    SUBFRAME->executor = &Evaluator_Executor;
    Restart_Evaluator_Frame(SUBFRAME);
    return CONTINUE_SUBFRAME(SUBFRAME);  // one step to pass predicate, see [1]

} condition_result_in_spare: {  //////////////////////////////////////////////

    Decay_If_Isotope(SPARE);

    if (Is_Void(SPARE))  // skip void expressions, see [2]
        goto handle_next_clause;

    if (Is_Frame_At_End(SUBFRAME))
        goto reached_end;  // we tolerate "fallout" from a condition

    if (Is_Nulled(predicate))
        goto processed_result_in_spare;

    STATE = ST_CASE_RUNNING_PREDICATE;
    SUBFRAME->executor = &Just_Use_Out_Executor;
    return CONTINUE(SPARE, predicate, SPARE);  // with == out is legal

} predicate_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Void(SPARE))  // error on void predicate results (not same as [2])
        fail (Error_Bad_Void());

    goto processed_result_in_spare;

} processed_result_in_spare: {  //////////////////////////////////////////////

    bool matched = Is_Truthy(SPARE);  // errors on most isotopes

    Cell(const*) branch = Lookback_While_Fetching_Next(SUBFRAME);

    if (not matched) {
        if (not IS_GET_GROUP(branch))
            goto handle_next_clause;
        else {
            // GET-GROUP! run even on no-match (see IF), but result discarded
        }

        Frame(*) sub = Make_Frame_At_Core(
            branch,  // turning into feed drops cell type, :(...) not special
            FRM_SPECIFIER(SUBFRAME),
            FRAME_MASK_NONE
        );
        sub->executor = &Array_Executor;

        STATE = ST_CASE_DISCARDING_GET_GROUP;
        SUBFRAME->executor = &Just_Use_Out_Executor;
        Push_Frame(discarded, sub);
        return CONTINUE_SUBFRAME(sub);
    }

    STATE = ST_CASE_RUNNING_BRANCH;
    SUBFRAME->executor = &Just_Use_Out_Executor;
    return CONTINUE_CORE(
        OUT,
        FRAME_FLAG_BRANCH,
        FRM_SPECIFIER(SUBFRAME), branch,
        SPARE
    );

} check_discarded_product_was_branch: {  /////////////////////////////////////

    if (not (FLAGIT_KIND(VAL_TYPE(discarded)) & TS_BRANCH))
        fail (Error_Bad_Value_Raw(discarded));  // like IF, see [3]

    goto handle_next_clause;

} branch_result_in_out: {  ///////////////////////////////////////////////////

    if (not REF(all)) {
        Drop_Frame(SUBFRAME);
        return BRANCHED(OUT);
    }

    goto handle_next_clause;

} reached_end: {  ////////////////////////////////////////////////////////////

    assert(REF(all) or Is_Fresh(OUT));  // never ran a branch, or running /ALL

    Drop_Frame(SUBFRAME);

    if (not Is_Fresh(SPARE)) {  // prioritize fallout result, see [4]
        Isotopify_If_Nulled(SPARE);
        Move_Cell(OUT, SPARE);
        return BRANCHED(OUT);
    }

    if (Is_Fresh(OUT))  // none of the clauses of an /ALL ran a branch
        return Finalize_Void(OUT);

    return BRANCHED(OUT);
}}


//
//  switch: native [
//
//  {Selects a choice and evaluates the block that follows it.}
//
//      return: "Last case evaluation, or null if no cases matched"
//          [<opt> <void> any-value!]
//      value "Target value"
//          [<opt> any-value!]
//      cases "Block of cases (comparison lists followed by block branches)"
//          [block!]
//      /all "Evaluate all matches (not just first one)"
//      /predicate "Binary switch-processing action (default is EQUAL?)"
//          [action! ~action!~]
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
{
    INCLUDE_PARAMS_OF_SWITCH;

    REBVAL *left = ARG(value);
    REBVAL *predicate = ARG(predicate);
    REBVAL *cases = ARG(cases);

    Value(*) scratch = LOCAL(scratch);

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
            Drop_Frame(SUBFRAME);
            return BRANCHED(OUT);
        }
        goto next_switch_step;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    assert(Is_Fresh(SPARE));  // initial condition
    assert(Is_Fresh(OUT));  // if no writes to out performed, we act void

    if (REF(predicate))
        Decay_If_Activation(predicate);

    if (IS_BLOCK(left) and Get_Cell_Flag(left, UNEVALUATED))
        fail (Error_Block_Switch_Raw(left));  // `switch [x] [...]` safeguard

    Frame(*) subframe = Make_Frame_At(
        cases,
        FRAME_FLAG_TRAMPOLINE_KEEPALIVE
    );

    Push_Frame(SPARE, subframe);

} next_switch_step: {  ///////////////////////////////////////////////////////

    FRESHEN(SPARE);  // fallout must be reset each time

    if (Is_Frame_At_End(SUBFRAME))
        goto reached_end;

    Cell(const*) at = At_Frame(SUBFRAME);

    if (IS_BLOCK(at) or IS_ACTION(at)) {  // seen with no match in effect
        Fetch_Next_Forget_Lookback(SUBFRAME);  // just skip over it
        goto next_switch_step;
    }

    STATE = ST_SWITCH_EVALUATING_RIGHT;
    SUBFRAME->executor = &Evaluator_Executor;
    Restart_Evaluator_Frame(SUBFRAME);
    return CONTINUE_SUBFRAME(SUBFRAME);  // no direct predicate call, see [1]

} right_result_in_spare: {  //////////////////////////////////////////////////

    if (Is_Void(SPARE))  // skip comments or failed conditionals
        goto next_switch_step;  // see note [2] in comments for CASE

    if (Is_Frame_At_End(SUBFRAME))
        goto reached_end;  // nothing left, so drop frame and return

    if (Is_Nulled(predicate)) {
        Decay_If_Isotope(SPARE);
        if (Is_Matcher(SPARE)) {
            if (not Matcher_Matches(SPARE, left))
                goto next_switch_step;
        }
        else {
            const bool strict = false;
            Copy_Cell(scratch, left);
            if (0 != Compare_Modify_Values(scratch, SPARE, strict))
                goto next_switch_step;
        }
    }
    else {
        // `switch x .greater? [10 [...]]` acts like `case [x > 10 [...]]
        // The ARG(value) passed in is the left/first argument to compare.
        //
        if (rebRunThrows(
            scratch,  // <-- output cell
            predicate,
                rebQ(left),  // first arg (left hand side if infix)
                rebQ(SPARE)  // second arg (right hand side if infix)
        )){
            return BOUNCE_THROWN;  // aborts subframe
        }
        if (Is_Falsey(scratch))
            goto next_switch_step;
    }

    Cell(const*) at = Try_At_Frame(SUBFRAME);

    while (true) {  // skip ahead for BLOCK!/ACTION! to process the match
        if (at == nullptr)
            goto reached_end;

        if (IS_BLOCK(at) or IS_META_BLOCK(at) or IS_ACTION(at))
            break;

        Fetch_Next_Forget_Lookback(SUBFRAME);
        at = At_Frame(SUBFRAME);
    }

    STATE = ST_SWITCH_RUNNING_BRANCH;
    SUBFRAME->executor = &Just_Use_Out_Executor;
    return CONTINUE_CORE(
        FRESHEN(OUT),
        FRAME_FLAG_BRANCH,
        FRM_SPECIFIER(SUBFRAME), at
    );

} reached_end: {  ////////////////////////////////////////////////////////////

    assert(REF(all) or Is_Fresh(OUT));

    Drop_Frame(SUBFRAME);

    if (not Is_Fresh(SPARE)) {  // see remarks in CASE on fallout prioritization
        Move_Cell(OUT, SPARE);
        return BRANCHED(OUT);
    }

    if (Is_Fresh(OUT))  // no fallout, and no branches ran
        return Finalize_Void(OUT);

    return BRANCHED(OUT);
}}


//
//  default: enfix native [
//
//  {Set word or path to a default value if it is not set yet}
//
//      return: "Former value or branch result, can only be null if no target"
//          [<opt> any-value!]
//      :target "Word or path which might be set appropriately (or not)"
//          [set-group! set-word! set-tuple!]  ; to left of DEFAULT
//      :branch "If target needs default, this is evaluated and stored there"
//          [any-branch!]
//      /predicate "Test for what's considered empty (default is null + void)"
//          [action! ~action!~]
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
//    to mean "emptiness" as candidates for overriding.  So isotopes do not
//    count as being unset.
{
    INCLUDE_PARAMS_OF_DEFAULT;

    REBVAL *target = ARG(target);
    REBVAL *branch = ARG(branch);
    REBVAL *predicate = ARG(predicate);

    REBVAL *steps = ARG(return);  // reuse slot to save resolved steps, see [1]

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

    if (Get_Var_Core_Throws(OUT, steps, target, SPECIFIED))  // see [1]
        return THROWN;

    if (not Is_Nulled(predicate)) {
        Decay_If_Activation(predicate);
        STATE = ST_DEFAULT_RUNNING_PREDICATE;
        return CONTINUE(SPARE, predicate, OUT);
    }

    if (not Is_Nihil(OUT) and not Is_Nulled(OUT))
        return OUT;  // consider it a "value", see [2]

    STATE = ST_DEFAULT_EVALUATING_BRANCH;
    return CONTINUE(SPARE, branch, OUT);

} predicate_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Truthy(SPARE))
        return OUT;

    STATE = ST_DEFAULT_EVALUATING_BRANCH;
    return CONTINUE(SPARE, branch, OUT);

} branch_result_in_spare: {  /////////////////////////////////////////////////

    if (Set_Var_Core_Throws(OUT, nullptr, steps, SPECIFIED, SPARE)) {
        assert(false);  // shouldn't be able to happen.
        fail (Error_No_Catch_For_Throw(FRAME));
    }

    return COPY(SPARE);
}}


//
//  catch: native [
//
//  {Catches a throw from a block and returns its value.}
//
//      return: "Thrown value, or BLOCK! with value and name (if /NAME, /ANY)"
//          [<opt> any-value!]
//      @result "Evaluation result (only set if not thrown)"
//          [<opt> any-value!]
//      block "Block to evaluate"
//          [block!]
//      /name "Catches a named throw (single name if not block)"
//          [block! word! action! object!]
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

    if (not THROWING) {
        Copy_Cell(ARG(result), OUT);
        Init_Nulled(OUT); // no throw means just return null
        return Proxy_Multi_Returns(frame_);
    }

    const REBVAL *label = VAL_THROWN_LABEL(FRAME);

    if (REF(any) and not (
        IS_ACTION(label)
        and ACT_DISPATCHER(VAL_ACTION(label)) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(quit) and (
        IS_ACTION(label)
        and ACT_DISPATCHER(VAL_ACTION(label)) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(name)) {
        //
        // We use equal? by way of Compare_Modify_Values, and re-use the
        // refinement slots for the mutable space

        REBVAL *temp1 = ARG(quit);
        REBVAL *temp2 = ARG(any);

        if (IS_BLOCK(ARG(name))) {
            //
            // Test all the words in the block for a match to catch

            Cell(const*) tail;
            Cell(const*) candidate = VAL_ARRAY_AT(&tail, ARG(name));
            for (; candidate != tail; candidate++) {
                //
                // !!! Should we test a typeset for illegal name types?
                //
                if (IS_BLOCK(candidate))
                    fail (PARAM(name));

                Derelativize(temp1, candidate, VAL_SPECIFIER(ARG(name)));
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

    if (REF(name) or REF(any)) {
        Array(*) a = Make_Array(2);
        SET_SERIES_LEN(a, 2);
        Copy_Cell(ARR_AT(a, 0), label); // throw name
        CATCH_THROWN(ARR_AT(a, 1), frame_); // thrown value--may be null!
        if (Is_Nulled(ARR_AT(a, 1)))  // can't put null in blocks...
            Init_Blank(ARR_AT(a, 1));  // !!! review: use multi-return instead!

        return Init_Block(OUT, a);
    }

    CATCH_THROWN(OUT, frame_); // thrown value

    if (Is_Void(OUT))
        return NONE;  // void would trigger ELSE

    Isotopify_If_Nulled(OUT);  // a caught NULL triggers THEN, not ELSE
    return BRANCHED(OUT);
}}


//
//  throw: native [
//
//  "Throws control back to a previous catch."
//
//      return: []  ; !!! notation for divergent function?
//      ^value "Value returned from catch"
//          [<opt> <void> any-value!]
//      /name "Throws to a named catch"
//          [word! action! object!]
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

    REBVAL *v = ARG(value);

    if (Is_Nulled(v))
        Init_Void(v);  // CONTINUE and CONTINUE VOID act the same
    else
        Meta_Unquotify(v);

    if (Is_Void(v))
        Init_Heavy_Void(v);
    else if (Is_Nulled(v))
        Init_Heavy_Null(v);

    return Init_Thrown_With_Label(FRAME, v, ARG(name));  // name can be nulled
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
void Debranch_Output(Value(*) out) {
    if (Is_Lazy(out)) {
        //
        // We don't have to fully reify the object, we just need to make sure
        // its THEN and ELSE fields are unset.
        //
        Symbol(const*) syms[2] = {Canon(ELSE), Canon(THEN)};
        int i;
        for (i = 0; i < 2; ++i) {
            option(Value(*)) hook = Select_Symbol_In_Context(out, syms[i]);
            if (hook)
                Init_Void(unwrap(hook));
        }
    }
    else if (Is_Void(out))
        Init_Heavy_Void(out);
    else if (Is_Nulled(out))
        Init_Heavy_Null(out);
}


//
//  Pushed_Reifying_Frame: C
//
bool Pushed_Reifying_Frame(Value(*) out, Value(const*) obj, Flags flags) {
    option(Value(*)) reify = Select_Symbol_In_Context(obj, Canon(REIFY));
    if (not reify)
        fail ("Asked to reify lazy object with no REIFY method");

    bool pushed = Pushed_Continuation(
        out,
        flags,
        SPECIFIED,
        unwrap(reify),
        nullptr  // no argument to reify--must answer in isolation
    );
    if (not pushed)
        return false;

    return true;
}
