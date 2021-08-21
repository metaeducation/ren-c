//
//  File: %n-control.c
//  Summary: "native functions for control flow"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
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
// * If they do not run any branches, the construct returns NULL...which is
//   not an ANY-VALUE! and can't be put in a block or assigned to a variable
//   (via SET-WORD! or SET-PATH!).  This is systemically the sign of a "soft
//   failure", and can signal constructs like ELSE, ALSO, TRY, etc.
//
// * If a branch *does* run--and that branch evaluation produces a NULL--then
//   conditionals designed to be used with branching (like IF or CASE) will
//   return a special "heavy" isotope variant of NULL (aka "NULL-2").  It acts
//   just like NULL in most cases, but for the purposes of ELSE and THEN it
//   is considered a signal that a branch ran.
//
// * Zero-arity function values used as branches will be executed, and
//   single-arity functions used as branches will also be executed--but passed
//   the value of the triggering condition.  See Do_Branch_Throws().
//
// * There is added checking that a literal block is not used as a condition,
//   to catch common mistakes like `if [x = 10] [...]`.
//

#include "sys-core.h"


//
//  if: native [
//
//  {When TO LOGIC! CONDITION is true, execute branch}
//
//      return: "null if branch not run, otherwise branch result"
//          [<opt> any-value!]
//      condition [<opt> any-value!]
//      :branch "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch!]
//  ]
//
REBNATIVE(if)
{
    INCLUDE_PARAMS_OF_IF;

    if (IS_CONDITIONAL_FALSE(ARG(condition)))
        return nullptr;  // ^-- truth test fails on voids, literal blocks

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(condition)))
        return R_THROWN;  // ^-- condition is passed to branch if function

    return D_OUT;  // most branch executions mark NULL as "heavy" isotope
}


//
//  either: native [
//
//  {Choose a branch to execute, based on TO-LOGIC of the CONDITION value}
//
//      return: [<opt> any-value!]
//          "Returns null if either branch returns null (unlike IF...ELSE)"
//      condition [<opt> any-value!]
//      :true-branch "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch!]
//      :false-branch
//          [any-branch!]
//  ]
//
REBNATIVE(either)
{
    INCLUDE_PARAMS_OF_EITHER;

    REBVAL *branch = IS_CONDITIONAL_TRUE(ARG(condition))
        ? ARG(true_branch)  // ^-- truth test fails on voids, literal blocks
        : ARG(false_branch);

    if (Do_Branch_With_Throws(D_OUT, branch, ARG(condition)))
        return R_THROWN;  // ^-- condition is passed to branch if function

    return D_OUT;  // most branch executions mark NULL as "heavy" isotope
}


inline static bool Single_Test_Throws(
    REBVAL *out,  // GC-safe output cell
    const RELVAL *test,
    REBSPC *test_specifier,
    const RELVAL *arg,
    REBSPC *arg_specifier
){
    // Note the user could write `rule!: [integer! rule!]`, and then try to
    // `match rule! <infinite>`...have to worry about stack overflows here.
    //
    if (C_STACK_OVERFLOWING(&arg_specifier))
        Fail_Stack_Overflow();

    // !!! The MATCH dialect concept calls functions and needs GC safe space
    // to process the test into.  Although the `out` cell is presumed safe,
    // putting a processed test into out means running into problems with
    // trying to use the test and the output in the same expression:
    //
    //     Get_If_Word_Or_Path_Throws(out, ...);
    //     test = out;
    //     ...
    //     Init_Logic(out, Some_Comparison(test, ...));  // test aliases out
    //
    // This aliasing leads to working in some compilations but fail in others.
    // Make a GC guarded cell to keep this from happening, which requires
    // some awkward `goto`-ing to make sure it drops (if only it were C++ !)
    // Optimize when this experimental dialect gets a more serious treatment.
    //
    DECLARE_LOCAL (fetched_test);
    SET_END(fetched_test);
    PUSH_GC_GUARD(fetched_test);

    enum Reb_Kind test_kind = VAL_TYPE(test);

    // If test is a WORD! or PATH! then GET it.  To help keep things clear,
    // require GET-WORD! or GET-PATH! for actions to convey they are not being
    // invoked inline, and disallow them on non-actions to help discern them
    // (maybe relax that later)
    //
    //    maybe [integer! :even?] 4  ; this is ok
    //    maybe [:integer! even?] 4  ; this is not
    //
    if (
        test_kind == REB_WORD
        or test_kind == REB_GET_WORD or test_kind == REB_GET_PATH
    ){
        const bool push_refinements = false;

        DECLARE_LOCAL (dequoted_test);  // wouldn't need if Get took quoted
        Dequotify(Derelativize(dequoted_test, test, test_specifier));

        REBDSP lowest_ordered_dsp = DSP;
        if (Get_If_Word_Or_Path_Throws(  // !!! take any escape level?
            fetched_test,
            dequoted_test,
            SPECIFIED,
            push_refinements  // !!! Look into pushing e.g. `match :foo?/bar x`
        )){
            Copy_Cell(out, fetched_test);
            goto return_thrown;
        }

        assert(lowest_ordered_dsp == DSP); // would have made specialization
        UNUSED(lowest_ordered_dsp);

        if (IS_ACTION(fetched_test)) {
            if (IS_GET_WORD(dequoted_test) or IS_GET_PATH(dequoted_test)) {
                // ok
            } else
                fail ("ACTION! match rule must be GET-WORD!/GET-PATH!");
        }

        test = fetched_test;
        test_kind = VAL_TYPE(test);
        test_specifier = SPECIFIED;
    }

  blockscope {
    bool matched;  // compiler will catch paths that don't initialize this

    switch (test_kind) {
      case REB_NULL:  // more useful for NON NULL XXX than MATCH NULL XXX
        matched = (VAL_TYPE(arg) == REB_NULL);
        goto return_matched;

      case REB_PATH: {  // AND the tests together
        REBSPC *specifier = Derive_Specifier(test_specifier, test);

        DECLARE_LOCAL (temp);  // path element extraction buffer (if needed)
        SET_END(temp);
        PUSH_GC_GUARD(temp);  // !!! doesn't technically need a guard?

        REBLEN len = VAL_SEQUENCE_LEN(test);
        REBLEN i;
        for (i = 0; i < len; ++i) {
            const RELVAL *item = VAL_SEQUENCE_AT(temp, test, i);

            if (Single_Test_Throws(
                out,
                item,
                specifier,
                arg,
                arg_specifier
            )){
                DROP_GC_GUARD(temp);
                goto return_thrown;
            }

            if (not VAL_LOGIC(out))  {  // any ANDing failing skips block
                DROP_GC_GUARD(temp);
                goto return_out;
            }
        }
        DROP_GC_GUARD(temp);
        assert(VAL_LOGIC(out) == true);  // if all tests succeeded in block
        goto return_out; }  // return the LOGIC! truth, false=no throw

      case REB_BLOCK: {  // OR the tests together
        const RELVAL *item_tail;
        const RELVAL *item = VAL_ARRAY_AT(&item_tail, test);
        REBSPC *specifier = Derive_Specifier(test_specifier, test);
        for (; item != item_tail; ++item) {
            if (Single_Test_Throws(
                out,
                item,
                specifier,
                arg,
                arg_specifier
            )){
                goto return_thrown;
            }
            if (VAL_LOGIC(out) == true)  // test succeeded
                goto return_out;  // return the LOGIC! true
        }
        assert(not VAL_LOGIC(out));
        goto return_out; }

      case REB_LOGIC:  // test for "truthy" or "falsey"
        //
        // Note: testing a literal block for truth or falsehood could make
        // sense if the *test* varies (e.g. true or false from variable).
        // So IS_TRUTHY() is used here instead of IS_CONDITIONAL_TRUE()
        //
        matched = VAL_LOGIC(test) == IS_TRUTHY(arg);
        goto return_matched;

      case REB_ACTION: {
        DECLARE_LOCAL (arg_specified);
        Derelativize(arg_specified, arg, arg_specifier);
        Dequotify(arg_specified);  // e.g. '':refinement? wants unquoted
        PUSH_GC_GUARD(arg_specified);

        DECLARE_LOCAL (temp);  // test is in `out`
        bool threw = rebRunThrows(
            temp,
            true,  // `fully` (ensure argument consumed)
            SPECIFIC(test),
            NULLIFY_NULLED(arg_specified)  // nulled cells to nullptr for API
        );

        DROP_GC_GUARD(arg_specified);
        if (threw) {
            Copy_Cell(out, temp);
            goto return_thrown;
        }

        matched = IS_TRUTHY(temp);  // errors on BAD-WORD!
        goto return_matched; }

      case REB_DATATYPE:
        matched = VAL_TYPE_KIND(test) == VAL_TYPE(arg);
        goto return_matched;

      case REB_TYPESET:
        matched = TYPE_CHECK(test, VAL_TYPE(arg));
        break;

     case REB_TAG: {  // just support <opt> for now
        bool strict = false;
        matched = VAL_TYPE(arg) == REB_NULL
                and 0 == CT_String(test, Root_Opt_Tag, strict);
        goto return_matched; }

      case REB_INTEGER:  // interpret as length
        matched = ANY_SERIES_KIND(VAL_TYPE(arg))
                and VAL_LEN_AT(arg) == VAL_UINT32(test);
        goto return_matched;

      case REB_META_WORD: {
        matched = Matches_Fake_Type_Constraint(
            arg,
            cast(enum Reb_Symbol_Id, VAL_WORD_ID(test))
        );
        goto return_matched; }

      case REB_BAD_WORD:
        //
        // Is `non '~void~ x` worth supporting?
        //
      default:
        fail (Error_Invalid_Type(test_kind));
    }

  return_matched:
    Init_Logic(out, matched);

  return_out:
    DROP_GC_GUARD(fetched_test);
    return false;
  }

  return_thrown:
    DROP_GC_GUARD(fetched_test);
    return true;
}


//
//  Match_Core_Throws: C
//
// MATCH is based on the idea of running a group of tests represented by
// single items.  e.g. `match 2 block` would check to see if the block was
// length 2, and `match :even? num` would pass back the value if it were even.
//
// A block can pull together these single tests.  They are OR'd by default,
// but if you use PATH! inside them then those are AND'ed.  Hence:
//
//     match [block!/2 integer!/[:even?]] value
//
// ...that would either match a block of length 2 or an even integer.
//
// In the quoted era, the concept is that match ['integer!] x would match '2.
//
// !!! Future directions may allow `match :(> 2) value` to auto-specialize a
// function to reduce it down to single arity so it can be called.
//
// !!! The choice of paths for the AND-ing rules is a bit edgy considering
// how wily paths are, but it makes sense (paths are minimum length 2, and
// no need for an AND group of length 1)...and allows for you to define a
// rule and then reuse it by reference from a word and know if it's an AND
// rule or an OR'd rule.
//
bool Match_Core_Throws(
    REBVAL *out, // GC-safe output cell
    const RELVAL *test,
    REBSPC *test_specifier,
    const RELVAL *arg,
    REBSPC *arg_specifier
){
    if (Single_Test_Throws(
        out,
        test,
        test_specifier,
        arg,
        arg_specifier
    )){
        return true;
    }

    assert(IS_LOGIC(out));
    return false;
}


//
//  else: enfix native [
//
//  {If input is not null, return that value, otherwise evaluate the branch}
//
//      return: "Input value if not null, or branch result (possibly null)"
//          [<opt> any-value!]
//      optional "<deferred argument> Run branch if this is null"
//          [<opt> <meta> any-value!]
//      :branch [any-branch!]
//  ]
//
REBNATIVE(else)  // see `tweak :else #defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ELSE;

    if (not IS_NULLED(ARG(optional))) {
        //
        // We have to use a ^meta parameter in order to detect the
        // difference between NULL (which runs the branch) and a ~null~
        // isotope (which does not run the branch).  But we don't want to
        // actually return a quoted parameter.
        //
        RETURN (Meta_Unquotify(ARG(optional)));
    }

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), Lib(NULL)))
        return R_THROWN;

    return D_OUT;  // note NULL branches will have been converted to NULL-2
}


//
//  else?: native [
//
//  {Determine if argument would have triggered an ELSE branch}
//
//      return: [logic!]
//      optional "Argument to test (note that WORD!-fetch would decay NULL-2)"
//          [<opt> <meta> any-value!]
//  ]
//
REBNATIVE(else_q)
{
    INCLUDE_PARAMS_OF_ELSE_Q;
    return Init_Logic(D_OUT, IS_NULLED(ARG(optional)));
}


//
//  then: enfix native [
//
//  {If input is null, return null, otherwise evaluate the branch}
//
//      return: "null if input is null, or branch result (voided if null)"
//          [<opt> any-value!]
//      optional "<deferred argument> Run branch if this is not null"
//          [<opt> <meta> any-value!]
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [any-branch!]
//  ]
//
REBNATIVE(then)  // see `tweak :then #defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_THEN;

    REBVAL *optional = ARG(optional);

    if (IS_NULLED(optional))
        return nullptr;  // left didn't run, so signal THEN didn't run either

    // We received the left hand side as ^meta, so it's quoted in order
    // to tell the difference between the NULL and ~null~ isotope.  Now that's
    // tested, unquote it back to normal.
    //
    Meta_Unquotify(optional);

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), optional))
        return R_THROWN;

    return D_OUT;  // note NULL branches will have been converted to NULL-2
}


//
//  then?: native [
//
//  {Determine if argument would have triggered a THEN branch}
//
//      return: [logic!]
//      optional "Argument to test (note that WORD!-fetch would decay NULL-2)"
//          [<opt> <meta> any-value!]
//  ]
//
REBNATIVE(then_q)
{
    INCLUDE_PARAMS_OF_THEN_Q;
    return Init_Logic(D_OUT, not IS_NULLED(ARG(optional)));
}


//
//  also: enfix native [
//
//  {For non-null input, evaluate and discard branch (like a pass-thru THEN)}
//
//      return: "The same value as input, regardless of if branch runs"
//          [<opt> any-value!]
//      optional "<deferred argument> Run branch if this is not null"
//          [<opt> <meta> any-value!]
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [any-branch!]
//  ]
//
REBNATIVE(also)  // see `tweak :also #defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ALSO;  // `then func [x] [(...) :x]` => `also [...]`

    REBVAL *optional = ARG(optional);

    if (IS_NULLED(optional))
        return nullptr;  // telegraph original input, but don't run

    // We received the left hand side as ^meta, so it's quoted in order
    // to tell the difference between the NULL and a ~null~ isotope.  Now
    // that's tested we know it's not plain NULL, so put it back to normal.
    //
    Meta_Unquotify(optional);

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), optional))
        return R_THROWN;

    RETURN (optional);  // ran, but pass thru the original input
}


//
//  match*: native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input if it matched, otherwise branch result"
//          [<opt> any-value!]
//      :test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              word!  ; GET to find actual test
//              action! get-word! get-path!  ; arity-1 filter function
//              path!  ; AND'd tests
//              block!  ; OR'd tests
//              datatype! typeset!  ; literals accepted
//              logic!  ; tests TO-LOGIC compatibility
//              tag!  ; just <opt> for now
//              integer!  ; matches length of series
//          ]
//      value [<opt> any-value!]
//      /not "Invert the result of the the test (used by NON)"
//      /safe "Return ~falsey~ isotopes to avoid IF MATCH LOGIC! FALSE bugs"
//  ]
//
REBNATIVE(match_p)
{
    INCLUDE_PARAMS_OF_MATCH_P;

    REBVAL *v = ARG(value);

    if (Match_Core_Throws(D_OUT, ARG(test), SPECIFIED, v, SPECIFIED))
        return R_THROWN;

    if (did REF(_not_) == VAL_LOGIC(D_OUT)) {
        //
        // Note: There's no good value we can return to signal failure
        // in a safe mode here:
        //
        //    if not non null (null) [print "user intended this to run"]
        //    non null (null) else [print "user intended this to run"]
        //
        // If we return nullptr, we break the first case.  But if we return
        // anything else--like a void to cause a tripwire on the first case--
        // we break the second case.
        //
        // So we have to fail here if trying to be safe.  TBD: Improve error.
        //
        if (REF(_not_))
            if (REF(safe) and not IS_BAD_WORD(v) and IS_FALSEY(v))
                fail ("Maybe-confusing NULL return in failed MATCH*/SAFE/NOT");

        return nullptr;  // light null isotope if no match
    }

    if (REF(safe) and not IS_BAD_WORD(v) and IS_FALSEY(v)) {
        //
        // In "safe" operation, falsey matched values return BAD-WORD! to show
        // they did match, but to avoid misleading falseness of the result.
        //
        return Init_Isotope(D_OUT, SYM_FALSEY);
    }

    Move_Cell(D_OUT, v);  // Otherwise, input is the result

    // We turn nulls that matched the rule into heavy nulls.
    //
    //    >> match [<opt> integer!] null then [print "We want this to run!"]
    //    We want this to run!
    //
    Isotopify_If_Nulled(D_OUT);
    return D_OUT;
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
REBNATIVE(must)  // `must x` is a faster synonym for `non null x`
{
    INCLUDE_PARAMS_OF_MUST;

    if (IS_NULLED(ARG(value)))
        fail ("MUST requires argument to not be NULL");

    RETURN (ARG(value));
}


//
//  all: native [
//
//  {Short-circuiting variant of AND, using a block of expressions as input}
//
//      return: "Product of last passing evaluation if all truthy, else null"
//          [<opt> any-value!]
//      'predicate "Test for whether an evaluation passes (default is .DID)"
//          [<skip> predicate! action!]
//      block "Block of expressions"
//          [block!]
//  ]
//
REBNATIVE(all)
{
    INCLUDE_PARAMS_OF_ALL;

    REBVAL *predicate = ARG(predicate);
    if (Cache_Predicate_Throws(D_OUT, predicate))
        return R_THROWN;

    DECLARE_FRAME_AT (f, ARG(block), EVAL_MASK_DEFAULT);
    Push_Frame(nullptr, f);

    Init_Void(D_OUT);  // `all []` is a ~void~ isotope

    do {
        // We write to the spare in case evaluation produces a ~void~ isotope
        // and we want to keep the previous value in D_OUT.
        //
        Init_Void(D_SPARE);
        if (Eval_Step_Maybe_Stale_Throws(D_SPARE, f)) {
            Move_Cell(D_OUT, D_SPARE);
            Abort_Frame(f);
            return R_THROWN;
        }
        CLEAR_CELL_FLAG(D_SPARE, OUT_NOTE_STALE);  // don't leak stale flag

        if (Is_Void(D_SPARE)) {  // may have been stale or not
            if (IS_END(f->feed->value))  // `all []`
                break;
            continue;  // `all [comment "hi" 1]`, first step is stale
        }

        if (IS_NULLED(predicate)) {  // default predicate effectively .DID
            if (IS_FALSEY(D_SPARE)) {  // false/blank/null triggers failure
                Abort_Frame(f);
                return nullptr;
            }
        }
        else {
            DECLARE_LOCAL (temp);  // D_SPARE and D_OUT both in use
            if (rebRunThrows(
                temp,
                true,
                rebINLINE(predicate),
                NULLIFY_NULLED(D_SPARE)
            )){
                return R_THROWN;
            }

            if (IS_FALSEY(temp)) {
                Abort_Frame(f);
                return nullptr;
            }
        }

        // spare passed the test and wasn't invisible, make it the new result
        //
        Move_Cell(D_OUT, D_SPARE);

    } while (NOT_END(f->feed->value));

    Drop_Frame(f);

    if (not IS_BAD_WORD(D_OUT) and IS_FALSEY(D_OUT)) {
        //
        // The only way a falsey evaluation should make it to the end is if a
        // predicate passed it.  Don't want that to trip up `if all` so make
        // it an isotope...but this way `all .not [null] then [<runs>]`
        //
        assert(not IS_NULLED(predicate));
        return Init_Isotope(D_OUT, SYM_FALSEY);
    }

    return D_OUT;
}


//
//  any: native [
//
//  {Short-circuiting version of OR, using a block of expressions as input}
//
//      return: "First passing evaluative result, or null if none pass"
//          [<opt> any-value!]
//      'predicate "Test for whether an evaluation passes (default is .DID)"
//          [<skip> predicate! action!]
//      block "Block of expressions"
//          [block!]
//  ]
//
REBNATIVE(any)
{
    INCLUDE_PARAMS_OF_ANY;

    REBVAL *predicate = ARG(predicate);
    if (Cache_Predicate_Throws(D_OUT, predicate))
        return R_THROWN;

    DECLARE_FRAME_AT (f, ARG(block), EVAL_MASK_DEFAULT);
    Push_Frame(nullptr, f);

    do {
        Init_Void(D_OUT);
        if (Eval_Step_Maybe_Stale_Throws(D_OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }
        CLEAR_CELL_FLAG(D_OUT, OUT_NOTE_STALE);  // don't leak stale flag

        if (Is_Void(D_OUT)) {  // may have been stale or not
            if (IS_END(f->feed->value))
                break;
            continue;  // `any [comment "hi" 1]`, first step is stale
        }

        if (IS_NULLED(predicate)) {  // default predicate effectively .DID
            if (IS_TRUTHY(D_OUT)) {
                Abort_Frame(f);
                return D_OUT;  // successful ANY returns the value
            }
        }
        else {
            if (rebRunThrows(
                D_SPARE,
                true,
                rebINLINE(predicate),
                NULLIFY_NULLED(D_OUT)
            )){
                return R_THROWN;
            }

            if (IS_TRUTHY(D_SPARE)) {
                //
                // Don't let ANY return something falsey, but using an isotope
                // makes `any .not [null] then [<run>]` work
                //
                if (not IS_BAD_WORD(D_OUT) and IS_FALSEY(D_OUT))
                    Init_Isotope(D_OUT, SYM_FALSEY);
                Abort_Frame(f);
                return D_OUT;  // return input to the test, not result
            }
        }

    } while (NOT_END(f->feed->value));

    Drop_Frame(f);
    return nullptr;
}


//
//  case: native [
//
//  {Evaluates each condition, and when true, evaluates what follows it}
//
//      return: "Last matched case evaluation, or null if no cases matched"
//          [<opt> any-value!]
//      'predicate "Unary case-processing action (default is /DID)"
//          [<skip> predicate! action!]
//      cases "Conditions followed by branches"
//          [block!]
//      /all "Do not stop after finding first logically true case"
//      <local> branch last  ; temp GC-safe holding locations
//  ]
//
REBNATIVE(case)
{
    INCLUDE_PARAMS_OF_CASE;

    REBVAL *predicate = ARG(predicate);
    if (Cache_Predicate_Throws(D_OUT, predicate))
        return R_THROWN;

    DECLARE_FRAME_AT (f, ARG(cases), EVAL_MASK_DEFAULT);

    Init_Nulled(ARG(last));  // default return result

    Push_Frame(nullptr, f);

    while (true) {

        Init_Nulled(D_OUT);  // forget previous result, new case running

        // Feed the frame forward one step for predicate argument.
        //
        // NOTE: It may seem tempting to run PREDICATE from on `f` directly,
        // allowing it to take arity > 2.  Don't do this.  We have to get a
        // true/false answer *and* know what the right hand argument was, for
        // full case coverage and for DEFAULT to work.

        if (Eval_Step_Maybe_Stale_Throws(D_OUT, f))
            goto threw;

        if (IS_END(f_value)) {
            CLEAR_CELL_FLAG(D_OUT, OUT_NOTE_STALE);
            goto reached_end;
        }

        if (GET_CELL_FLAG(D_OUT, OUT_NOTE_STALE))
            continue;  // a COMMENT, but not at end.

        bool matched;
        if (IS_NULLED(predicate)) {
            matched = IS_TRUTHY(D_OUT);
        }
        else {
            DECLARE_LOCAL (temp);
            if (rebRunThrows(
                temp,
                true,  // fully = true (e.g. argument must be taken)
                rebINLINE(predicate),
                rebQ(D_OUT)  // argument
            )){
                goto threw;
            }
            matched = IS_TRUTHY(temp);
        }

        if (IS_GET_GROUP(f_value)) {
            //
            // IF evaluates branches that are GET-GROUP! even if it does
            // not run them.  This implies CASE should too.
            //
            // Note: Can't evaluate directly into ARG(branch)...frame cell.
            //
            if (Eval_Value_Throws(D_SPARE, f_value, f_specifier)) {
                Copy_Cell(D_OUT, D_SPARE);
                goto threw;
            }
            Copy_Cell(ARG(branch), D_SPARE);
        }
        else
            Derelativize(ARG(branch), f_value, f_specifier);

        Fetch_Next_Forget_Lookback(f);  // branch now in ARG(branch), so skip

        if (not matched) {
            if (not (FLAGIT_KIND(VAL_TYPE(ARG(branch))) & TS_BRANCH)) {
                //
                // Maintain symmetry with IF on non-taken branches:
                //
                // >> if false <some-tag>
                // ** Script Error: if does not allow tag! for its branch...
                //
                fail (Error_Bad_Value_Raw(ARG(branch)));
            }

            continue;
        }

        bool threw = Do_Branch_With_Throws(D_SPARE, ARG(branch), D_OUT);
        Move_Cell(D_OUT, D_SPARE);
        if (threw)
            goto threw;

        if (not REF(all)) {
            Drop_Frame(f);
            return D_OUT;
        }

        Move_Cell(ARG(last), D_OUT);
    }

  reached_end:;

    Drop_Frame(f);

    // Last evaluation will "fall out" if there is no branch:
    //
    //     case .not [1 < 2 [...] 3 < 4 [...] 10 + 20] = 30
    //
    if (not IS_NULLED(D_OUT)) // prioritize fallout result
        return D_OUT;

    assert(REF(all) or IS_NULLED(ARG(last)));
    RETURN (ARG(last));  // else last branch "falls out", may be null

  threw:;

    Abort_Frame(f);
    return R_THROWN;
}


//
//  switch: native [
//
//  {Selects a choice and evaluates the block that follows it.}
//
//      return: "Last case evaluation, or null if no cases matched"
//          [<opt> any-value!]
//      'predicate "Binary switch-processing action (default is .EQUAL?)"
//          [<skip> predicate! action!]
//      value "Target value"
//          [<opt> any-value!]
//      cases "Block of cases (comparison lists followed by block branches)"
//          [block!]
//      /all "Evaluate all matches (not just first one)"
//      <local> last  ; GC-safe storage loation
//  ]
//
REBNATIVE(switch)
{
    INCLUDE_PARAMS_OF_SWITCH;

    REBVAL *predicate = ARG(predicate);
    if (Cache_Predicate_Throws(D_OUT, predicate))
        return R_THROWN;

    DECLARE_FRAME_AT (f, ARG(cases), EVAL_MASK_DEFAULT);

    Push_Frame(nullptr, f);

    Init_Nulled(ARG(last));

    REBVAL *left = ARG(value);
    if (IS_BLOCK(left) and GET_CELL_FLAG(left, UNEVALUATED))
        fail (Error_Block_Switch_Raw(left));  // `switch [x] [...]` safeguard

    Init_Nulled(D_OUT);  // fallout result if no branches run

    while (NOT_END(f_value)) {

        if (IS_BLOCK(f_value) or IS_ACTION(f_value)) {
            Fetch_Next_Forget_Lookback(f);
            Init_Nulled(D_OUT);  // reset fallout output to null
            continue;
        }

        // Feed the frame forward...evaluate one step to get second argument.
        //
        // NOTE: It may seem tempting to run COMPARE from the frame directly,
        // allowing it to take arity > 2.  Don't do this.  We have to get a
        // true/false answer *and* know what the right hand argument was, for
        // full switching coverage and for DEFAULT to work.
        //
        // !!! Advanced frame tricks *might* make this possible for N-ary
        // functions, the same way `match parse "aaa" [some "a"]` => "aaa"

        if (Eval_Step_Throws(SET_END(D_OUT), f))
            goto threw;

        if (IS_END(D_OUT)) {
            if (NOT_END(f_value))  // was just COMMENT/etc. so more to go
                continue;

            Drop_Frame(f);  // nothing left, so drop frame and return

            assert(REF(all) or IS_NULLED(ARG(last)));
            RETURN (ARG(last));
        }

        if (IS_NULLED(predicate)) {
            //
            // It's okay that we are letting the comparison change `value`
            // here, because equality is supposed to be transitive.  So if it
            // changes 0.01 to 1% in order to compare it, anything 0.01 would
            // have compared equal to so will 1%.  (That's the idea, anyway,
            // required for `a = b` and `b = c` to properly imply `a = c`.)
            //
            // !!! This means fallout can be modified from its intent.  Rather
            // than copy here, this is a reminder to review the mechanism by
            // which equality is determined--and why it has to mutate.
            //
            // !!! A branch composed into the switch cases block may want to
            // see the un-mutated condition value.
            //
            const bool strict = false;
            if (0 != Compare_Modify_Values(left, D_OUT, strict))
                continue;
        }
        else {
            // `switch x .greater? [10 [...]]` acts like `case [x > 10 [...]]
            // The ARG(value) passed in is the left/first argument to compare.
            //
            // !!! Using Run_Throws loses the labeling of the function we were
            // given (label).  Consider how it might be passed through
            // for better stack traces and error messages.
            //
            // !!! We'd like to run this faster, so we aim to be able to
            // reuse this frame...hence D_SPARE should not be expected to
            // survive across this point.
            //
            DECLARE_LOCAL (temp);
            if (rebRunThrows(
                temp,
                true,  // fully = true (e.g. both arguments must be taken)
                rebINLINE(predicate),
                rebQ(left),  // first arg (left hand side if infix)
                rebQ(D_OUT)  // second arg (right hand side if infix)
            )){
                goto threw;
            }
            if (IS_FALSEY(temp))
                continue;
        }

        // Skip ahead to try and find BLOCK!/ACTION! branch to take the match
        //
        while (true) {
            if (IS_END(f_value))
                goto reached_end;

            if (IS_BLOCK(f_value) or IS_META_BLOCK(f_value)) {
                //
                // f_value is RELVAL, can't Do_Branch
                //
                if (Do_Any_Array_At_Throws(D_OUT, f_value, f_specifier))
                    goto threw;
                if (IS_BLOCK(f_value))
                    Isotopify_If_Nulled(D_OUT);
                break;
            }

            if (IS_ACTION(f_value)) {  // must have been COMPOSE'd in cases
                DECLARE_LOCAL (temp);
                if (rebRunThrows(
                    temp,
                    false,  // fully = false, e.g. arity-0 functions are ok
                    SPECIFIC(f_value),  // actions don't need specifiers
                    rebQ(D_OUT)
                )){
                    Move_Cell(D_OUT, temp);
                    goto threw;
                }
                Move_Cell(D_OUT, temp);
                break;
            }

            Fetch_Next_Forget_Lookback(f);
        }

        if (not REF(all)) {
            Drop_Frame(f);
            return D_OUT;
        }

        Copy_Cell(ARG(last), D_OUT);  // save in case no fallout
        Init_Nulled(D_OUT);  // switch back to using for fallout
        Fetch_Next_Forget_Lookback(f);  // keep matching if /ALL
    }

  reached_end:

    Drop_Frame(f);

    if (not IS_NULLED(D_OUT)) // prioritize fallout result
        return D_OUT;

    assert(REF(all) or IS_NULLED(ARG(last)));
    RETURN (ARG(last));  // else last branch "falls out", may be null

  threw:;

    Drop_Frame(f);
    return R_THROWN;
}


//
//  default: enfix native [
//
//  {Set word or path to a default value if it is not set yet}
//
//      return: "Former value or branch result, can only be null if no target"
//          [<opt> any-value!]
//      :target "Word or path which might be set appropriately (or not)"
//          [set-word! set-path! set-tuple!]  ; to left of DEFAULT
//      'predicate "Test beyond null/void for defaulting, else .NOT.BLANK?"
//          [<skip> predicate! action!]  ; to right of DEFAULT
//      :branch "If target needs default, this is evaluated and stored there"
//          [any-branch!]
//  ]
//
REBNATIVE(default)
{
    INCLUDE_PARAMS_OF_DEFAULT;

    REBVAL *target = ARG(target);

    REBVAL *predicate = ARG(predicate);
    if (Cache_Predicate_Throws(D_OUT, predicate))
        return R_THROWN;

    if (IS_SET_WORD(target))
        Copy_Cell(D_OUT, Lookup_Word_May_Fail(target, SPECIFIED));
    else {
        assert(IS_SET_PATH(target) or IS_SET_TUPLE(target));

        // We want to be able to default a path with groups in it, but don't
        // want to double-evaluate.  In a userspace DEFAULT we would do
        // COMPOSE on the PATH! and then use GET/HARD and SET/HARD.  To make
        // a faster native we just do a more optimal version of that.
        //
        REBLEN len = VAL_SEQUENCE_LEN(target);
        bool has_groups = false;
        REBLEN i;
        for (i = 0; i < len; ++i) {
            const RELVAL *item = VAL_SEQUENCE_AT(D_SPARE, target, i);

            if (IS_GROUP(item))
                has_groups = true;
        }
        if (has_groups) {
            REBARR *composed = Make_Array(len);
            RELVAL *dest = ARR_HEAD(composed);
            REBSPC *specifier = VAL_SPECIFIER(target);
            for (i = 0; i < len; ++i, ++dest) {
                const RELVAL *item = VAL_SEQUENCE_AT(D_SPARE, target, i);

                if (not IS_GROUP(item))
                    Derelativize(dest, item, VAL_SPECIFIER(target));
                else {
                    if (Do_Any_Array_At_Throws(D_OUT, item, specifier))
                        return R_THROWN;
                    Copy_Cell(dest, D_OUT);
                }
            }
            SET_SERIES_LEN(composed, len);
            Freeze_Array_Shallow(composed);
            Force_Series_Managed(composed);

            // !!! The limiting of path contents messes this up; you cannot
            // generically store path picking info if it's an arbitrary value
            // because not all values are allowed in paths.  This will require
            // rethinking!
            //
            if (not Try_Init_Any_Sequence_Arraylike(
                target,
                REB_SET_PATH,
                composed
            )){
                fail ("Cannot compose arbitrary path, review implications");
            }
        }

        if (Eval_Path_Throws_Core(
            D_OUT,
            target,  // !!! May not be array-based
            VAL_SPECIFIER(target),
            nullptr,  // not requesting value to set means it's a get
            EVAL_MASK_DEFAULT
                | EVAL_FLAG_PATH_HARD_QUOTE // pre-COMPOSE'd, GROUP!s literal
        )){
            panic (D_OUT); // shouldn't be possible... no executions!
        }
    }

    // We only consider those bad words which are in the "unfriendly" state
    // that the system also knows to mean "emptiness" as candidates for
    // overriding.  That's ~unset~ and ~void~ at the moment.  Note that
    // friendly ones do not count:
    //
    //     >> x: second [~void~ ~unset~]
    //     == ~unset~
    //
    //     >> x: default [10]
    //     == ~unset~
    //
    if (not (IS_NULLED(D_OUT) or Is_Unset(D_OUT) or Is_Void(D_OUT))) {
        if (not REF(predicate)) {  // no custom additional constraint
            if (not IS_BLANK(D_OUT))  // acts as `x: default .not.blank? [...]`
                return D_OUT;  // count it as "already set"
        }
        else {
            if (rebDid(rebINLINE(predicate), rebQ(D_OUT)))
                return D_OUT;
        }
    }

    if (Do_Branch_Throws(D_OUT, ARG(branch)))
        return R_THROWN;

    if (IS_SET_WORD(target))
        Copy_Cell(Sink_Word_May_Fail(target, SPECIFIED), D_OUT);
    else {
        assert(IS_SET_PATH(target) or IS_SET_TUPLE(target));
        DECLARE_LOCAL (dummy);
        if (Eval_Path_Throws_Core(
            dummy,
            target,  // !!! may not be array-based
            VAL_SPECIFIER(target),
            D_OUT,
            EVAL_MASK_DEFAULT
                | EVAL_FLAG_PATH_HARD_QUOTE  // precomposed, no double eval
        )){
            panic (dummy); // shouldn't be possible, no executions!
        }
    }
    return D_OUT;
}


//
//  catch: native [
//
//  {Catches a throw from a block and returns its value.}
//
//      return: "Thrown value, or BLOCK! with value and name (if /NAME, /ANY)"
//          [<opt> any-value!]
//      result: "<output> Evaluation result (only set if not thrown)"
//          [<opt> any-value!]
//
//      block "Block to evaluate"
//          [block!]
//      /name "Catches a named throw (single name if not block)"
//          [block! word! action! object!]
//      /quit "Special catch for QUIT native"
//      /any "Catch all throws except QUIT (can be used with /QUIT)"
//  ]
//
REBNATIVE(catch)
//
// There's a refinement for catching quits, and CATCH/ANY will not alone catch
// it (you have to CATCH/ANY/QUIT).  Currently the label for quitting is the
// NATIVE! function value for QUIT.
{
    INCLUDE_PARAMS_OF_CATCH;

    // /ANY would override /NAME, so point out the potential confusion
    //
    if (REF(any) and REF(name))
        fail (Error_Bad_Refines_Raw());

    if (not Do_Any_Array_At_Throws(D_OUT, ARG(block), SPECIFIED)) {
        if (REF(result))
            rebElide(Lib(SET), rebQ(REF(result)), rebQ(D_OUT));

        return nullptr;  // no throw means just return null
    }

    const REBVAL *label = VAL_THROWN_LABEL(D_OUT);

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

            const RELVAL *tail;
            const RELVAL *candidate = VAL_ARRAY_AT(&tail, ARG(name));
            for (; candidate != tail; candidate++) {
                //
                // !!! Should we test a typeset for illegal name types?
                //
                if (IS_BLOCK(candidate))
                    fail (PAR(name));

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
        if (IS_NULLED(label) and (REF(any) or not REF(quit)))
            goto was_caught;
    }

    return R_THROWN; // throw name is in D_OUT, value is held task local

  was_caught:

    if (REF(name) or REF(any)) {
        REBARR *a = Make_Array(2);

        Copy_Cell(ARR_AT(a, 0), label); // throw name
        CATCH_THROWN(ARR_AT(a, 1), D_OUT); // thrown value--may be null!
        if (IS_NULLED(ARR_AT(a, 1)))
            SET_SERIES_LEN(a, 1); // trim out null value (illegal in block)
        else
            SET_SERIES_LEN(a, 2);
        return Init_Block(D_OUT, a);
    }

    CATCH_THROWN(D_OUT, D_OUT); // thrown value
    Isotopify_If_Nulled(D_OUT);  // a caught NULL triggers THEN, not ELSE
    return D_OUT;
}


//
//  throw: native [
//
//  "Throws control back to a previous catch."
//
//      return: []  ; !!! notation for divergent function?
//      value "Value returned from catch"
//          [<opt> any-value!]
//      /name "Throws to a named catch"
//          [word! action! object!]
//  ]
//
REBNATIVE(throw)
//
// Choices are currently limited for what one can use as a "name" of a THROW.
// Note blocks as names would conflict with the `name_list` feature in CATCH.
//
// !!! Should it be /NAMED instead of /NAME?
{
    INCLUDE_PARAMS_OF_THROW;

    return Init_Thrown_With_Label(
        D_OUT,
        ARG(value),
        ARG(name)  // NULLED if unused
    );
}
