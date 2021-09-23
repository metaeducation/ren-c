//
//  File: %n-control.c
//  Summary: "native functions for control flow"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
//   not an ANY-VALUE! and can't be put in a block.  This is systemically the
//   sign of a "soft failure", and cues constructs like ELSE, ALSO, TRY, etc.
//
//   (See %sys-nulled.h for information about the NULL state.)
//
// * If a branch *does* run--and its evaluation happens to produce NULL--then
//   conditionals designed to be used with branching (like IF or CASE) will
//   return a special "isotope" form of the BAD-WORD! of ~null~.  It's called
//   an "isotope" because it will "decay" into a regular NULL when assigned
//   to a variable.  But so long as it doesn't decay, then constructs like
//   ELSE and THEN consider it distinct from NULL, and hence a signal that a
//   branch actually ran.
//
//   (See %sys-bad-word.h for more details about isotopes.)
//
// * Some branch forms can subvert the conversion of NULL to isotopes.
//
// * Zero-arity function values used as branches will be executed, and
//   single-arity functions used as branches will also be executed--but passed
//   the value of the triggering condition.
//
//   (See Do_Branch_Throws() for supported ANY-BRANCH! types and behaviors.)
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

    return D_OUT;  // most branch executions convert NULL to a ~null~ isotope
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
        ? ARG(true_branch)  // ^-- test errors on BAD-WORD!, literal blocks
        : ARG(false_branch);

    if (Do_Branch_With_Throws(D_OUT, branch, ARG(condition)))
        return R_THROWN;  // ^-- condition is passed to branch if function

    return D_OUT;  // most branch executions convert NULL to a ~null~ isotope
}


//
//  else: enfix native [
//
//  {If input is not null, return that value, otherwise evaluate the branch}
//
//      return: "Input value if not null, or branch result"
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

    return D_OUT;  // most branch executions convert NULL to a ~null~ isotope
}


//
//  else?: native [
//
//  {Determine if argument would have triggered an ELSE branch}
//
//      return: [logic!]
//      optional "Argument to test"
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
//      return: "null if input is null, or branch result"
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

    return D_OUT;  // most branch executions convert NULL to a ~null~ isotope
}


//
//  then?: native [
//
//  {Determine if argument would have triggered a THEN branch}
//
//      return: [logic!]
//      optional "Argument to test"
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
//  match: native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input if it matched, NULL if it did not (isotope if falsey)"
//          [<opt> any-value!]
//      test "Typeset or arity-1 filter function"
//          [<opt> logic! action! block! datatype! typeset!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(match)
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
        if (not IS_NULLED(v))
            return nullptr;
        break;

      case REB_LOGIC:
        if (IS_TRUTHY(v) != VAL_LOGIC(test))
            return nullptr;
        break;

      case REB_DATATYPE:
        if (VAL_TYPE(v) != VAL_TYPE_KIND(test))
            return nullptr;
        break;

      case REB_BLOCK: {
        REB_R r = MAKE_Typeset(D_SPARE, REB_TYPESET, nullptr, test);
        if (r == R_THROWN) {
            Move_Cell(D_OUT, D_SPARE);
            return R_THROWN;
        }
        test = D_SPARE;
        goto test_is_typeset; }

      case REB_TYPESET:
      test_is_typeset:
        if (not TYPE_CHECK(test, VAL_TYPE(v)))
            return nullptr;
        break;

      case REB_ACTION:
        if (rebRunThrows(D_SPARE, true, test, rebQ(v))) {
            Move_Cell(D_OUT, D_SPARE);
            return R_THROWN;
        }
        if (IS_FALSEY(D_SPARE))
            return nullptr;
        break;

      default:
        fail (PAR(test));  // all test types should be accounted for in switch
    }

    //=//// IF IT GOT THIS FAR WITHOUT RETURNING, THE TEST MATCHED /////////=//

    // Falsey matched values return isotopes to show they did match, but to
    // avoid misleading falseness of the result:
    //
    //     >> value: false
    //     >> if match [integer! logic!] value [print "Won't run :-("]
    //     ; null  <-- this would be a bad result!
    //
    // So successful matching of falsey values will give back ~false~, ~blank~,
    // or ~null~.  This can be consciously turned back into their original
    // values with DECAY.
    //
    //     >> match blank! _
    //     == ~blank~
    //
    //     >> decay match blank! _
    //     == _
    //
    Isotopify_If_Falsey(v);

    Move_Cell(D_OUT, v);  // Otherwise, input is the result
    Init_Isotope(v, Canon(MOVE));  // Can't have a moved-from cell in frame
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
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! the-block!]
//      /predicate "Test for whether an evaluation passes (default is DID)"
//          [action!]
//  ]
//
REBNATIVE(all)
{
    INCLUDE_PARAMS_OF_ALL;

    REBVAL *predicate = ARG(predicate);

    REBFLGS flags = EVAL_MASK_DEFAULT;
    if (IS_THE_BLOCK(ARG(block)))
        flags |= EVAL_FLAG_NO_EVALUATIONS;

    DECLARE_FRAME_AT (f, ARG(block), flags);
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
                predicate,
                rebQ(NULLIFY_NULLED(D_SPARE))
            )){
                Move_Cell(D_OUT, temp);
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

    // The only way a falsey evaluation should make it to the end is if a
    // predicate passed it.  Don't want that to trip up `if all` so make it
    // an isotope...but this way `(all/predicate [null] :not?) then [<runs>]`
    //
    if (not IS_BAD_WORD(D_OUT) and IS_FALSEY(D_OUT))
        assert(not IS_NULLED(predicate));

    Isotopify_If_Falsey(D_OUT);

    return D_OUT;
}


//
//  any: native [
//
//  {Short-circuiting version of OR, using a block of expressions as input}
//
//      return: "First passing evaluative result, or null if none pass"
//          [<opt> any-value!]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! the-block!]
//      /predicate "Test for whether an evaluation passes (default is DID)"
//          [action!]
//  ]
//
REBNATIVE(any)
{
    INCLUDE_PARAMS_OF_ANY;

    REBVAL *predicate = ARG(predicate);

    REBFLGS flags = EVAL_MASK_DEFAULT;
    if (IS_THE_BLOCK(ARG(block)))
        flags |= EVAL_FLAG_NO_EVALUATIONS;

    DECLARE_FRAME_AT (f, ARG(block), flags);
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
                predicate,
                rebQ(NULLIFY_NULLED(D_OUT))
            )){
                Move_Cell(D_OUT, D_SPARE);
                return R_THROWN;
            }

            if (IS_TRUTHY(D_SPARE)) {
                //
                // Don't let ANY return something falsey, but using an isotope
                // makes `any .not [null] then [<run>]` work
                //
                Isotopify_If_Falsey(D_OUT);

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
//      cases "Conditions followed by branches"
//          [block!]
//      /all "Do not stop after finding first logically true case"
//      <local> branch last  ; temp GC-safe holding locations
//      /predicate "Unary case-processing action (default is DID)"
//          [action!]
//  ]
//
REBNATIVE(case)
{
    INCLUDE_PARAMS_OF_CASE;

    REBVAL *predicate = ARG(predicate);

    DECLARE_FRAME_AT (f, ARG(cases), EVAL_MASK_DEFAULT);

    assert(Is_Unset(ARG(last)));
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
                predicate,
                rebQ(D_OUT)  // argument
            )){
                Move_Cell(D_OUT, temp);
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
                Move_Cell(D_OUT, D_SPARE);
                goto threw;
            }
            Move_Cell(ARG(branch), D_SPARE);
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
//      value "Target value"
//          [<opt> any-value!]
//      cases "Block of cases (comparison lists followed by block branches)"
//          [block!]
//      /all "Evaluate all matches (not just first one)"
//      /predicate "Binary switch-processing action (default is EQUAL?)"
//          [action!]
//      <local> last  ; GC-safe storage loation
//  ]
//
REBNATIVE(switch)
{
    INCLUDE_PARAMS_OF_SWITCH;

    REBVAL *predicate = ARG(predicate);

    DECLARE_FRAME_AT (f, ARG(cases), EVAL_MASK_DEFAULT);

    Push_Frame(nullptr, f);

    assert(Is_Unset(ARG(last)));
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

        if (Eval_Step_Throws(D_OUT, f))
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
                predicate,
                rebQ(left),  // first arg (left hand side if infix)
                rebQ(D_OUT)  // second arg (right hand side if infix)
            )){
                Move_Cell(D_OUT, temp);
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
//      :branch "If target needs default, this is evaluated and stored there"
//          [any-branch!]
//      /predicate "Test beyond null/void for defaulting, else NOT BLANK?"
//          [action!]
//  ]
//
REBNATIVE(default)
{
    INCLUDE_PARAMS_OF_DEFAULT;

    REBVAL *target = ARG(target);

    REBVAL *predicate = ARG(predicate);

    if (IS_SET_WORD(target))
        Copy_Cell(D_OUT, Lookup_Word_May_Fail(target, SPECIFIED));
    else {
        assert(IS_SET_PATH(target) or IS_SET_TUPLE(target));
        //
        // !!! This is temporary, it will double evaluate any GROUP!s in
        // the path...a better solution is being worked on.
        //
        Get_Var_May_Fail(D_OUT, target, SPECIFIED, true);
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
            if (rebDid(predicate, rebQ(D_OUT)))
                return D_OUT;
        }
    }

    if (Do_Branch_Throws(D_OUT, ARG(branch)))
        return R_THROWN;

    if (IS_SET_WORD(target))
        Copy_Cell(Sink_Word_May_Fail(target, SPECIFIED), D_OUT);
    else {
        assert(IS_SET_PATH(target) or IS_SET_TUPLE(target));

        // !!! Again, this is temporary, as it means GROUP!s in any PATH!
        // would be evaluated twice in the defaulting process.
        //
        Set_Var_May_Fail(target, SPECIFIED, D_OUT, SPECIFIED);
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
