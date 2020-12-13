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
//   return a special variant of NULL (tentatively called "NULL-2").  It acts
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
    REBVAL *out, // GC-safe output cell
    const RELVAL *test,
    REBSPC *test_specifier,
    const RELVAL *arg,
    REBSPC *arg_specifier,
    REBLEN sum_quotes
){
    // Note the user could write `rule!: [integer! rule!]`, and then try to
    // `match rule! <infinite>`...have to worry about stack overflows here.
    //
    if (C_STACK_OVERFLOWING(&sum_quotes))
        Fail_Stack_Overflow();

    // We may need to add in the quotes of the dereference.  e.g.
    //
    //     >> quoted-word!: quote word!
    //     >> match ['quoted-word!] lit ''foo
    //     == ''foo
    //
    sum_quotes += VAL_NUM_QUOTES(test);

    REBCEL(const*) test_cell = VAL_UNESCAPED(test);
    REBCEL(const*) arg_cell = VAL_UNESCAPED(arg);

    enum Reb_Kind test_kind = CELL_KIND(test_cell);

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

        DECLARE_LOCAL (dequoted_test); // wouldn't need if Get took any escape
        Dequotify(Derelativize(dequoted_test, test, test_specifier));

        REBDSP lowest_ordered_dsp = DSP;
        if (Get_If_Word_Or_Path_Throws( // !!! take any escape level?
            out,
            dequoted_test,
            SPECIFIED,
            push_refinements // !!! Look into pushing e.g. `match :foo?/bar x`
        )){
            return true;
        }

        assert(lowest_ordered_dsp == DSP); // would have made specialization
        UNUSED(lowest_ordered_dsp);

        if (IS_ACTION(out)) {
            if (IS_GET_WORD(dequoted_test) or IS_GET_PATH(dequoted_test)) {
                // ok
            } else
                fail ("ACTION! match rule must be GET-WORD!/GET-PATH!");
        }
        else {
            sum_quotes += VAL_NUM_QUOTES(out);
            Dequotify(out); // we want to use the dequoted version for test
        }

        test = out;
        test_cell = VAL_UNESCAPED(test);
        test_kind = CELL_KIND(test_cell);
        test_specifier = SPECIFIED;
    }

    switch (test_kind) {
      case REB_NULL:  // more useful for NON NULL XXX than MATCH NULL XXX
        Init_Logic(
            out,
            CELL_KIND(arg_cell) == REB_NULL
                and VAL_NUM_QUOTES(arg) == sum_quotes
        );
        return false;

      case REB_VOID:
        //
        // Was considered because NON VOID XXX is shorter than NON VOID! XXX.
        // However, that encourages a habit of passing void values where they
        // probably are better caught as errors.
        //
        break;

      case REB_PATH: { // AND the tests together
        REBSPC *specifier = Derive_Specifier(test_specifier, test);

        // !!! The array case can extract VAL_ARRAY() from the test and
        // reuse `out`, but paths may not have series to extract... have to
        // cache the test in a GC-safe slot
        //
        DECLARE_LOCAL (test_cache);  // have to keep `out` alive
        Move_Value(test_cache, SPECIFIC(CELL_TO_VAL(test_cell)));
        PUSH_GC_GUARD(test_cache);

        DECLARE_LOCAL (temp);  // path element extraction buffer (if needed)
        SET_END(temp);
        PUSH_GC_GUARD(temp);

        REBLEN len = VAL_SEQUENCE_LEN(test_cache);
        REBLEN i;
        for (i = 0; i < len; ++i) {
            const RELVAL *item = VAL_SEQUENCE_AT(temp, test_cache, i);

            if (Single_Test_Throws(
                out,
                item,
                specifier,
                arg,
                arg_specifier,
                sum_quotes
            )){
                DROP_GC_GUARD(temp);
                DROP_GC_GUARD(test_cache);
                return true;
            }

            if (not VAL_LOGIC(out))  {  // any ANDing failing skips block
                DROP_GC_GUARD(temp);
                DROP_GC_GUARD(test_cache);
                return false;  // false=no throw
            }
        }
        DROP_GC_GUARD(temp);
        DROP_GC_GUARD(test_cache);
        assert(VAL_LOGIC(out)); // if all tests succeeded in block
        return false; } // return the LOGIC! truth, false=no throw

      case REB_BLOCK: { // OR the tests together
        const RELVAL *item = STABLE_HACK(VAL_ARRAY_AT(test_cell));
        REBSPC *specifier = Derive_Specifier(test_specifier, test);
        for (; NOT_END(item); ++item) {
            if (Single_Test_Throws(
                out,
                item,
                specifier,
                arg,
                arg_specifier,
                sum_quotes
            )){
                return true;
            }
            if (VAL_LOGIC(out)) // test succeeded
                return false; // return the LOGIC! truth, false=no throw
        }
        assert(not VAL_LOGIC(out));
        return false; }

      case REB_LOGIC: // test for "truthy" or "falsey"
        //
        // Note: testing a literal block for truth or falsehood could make
        // sense if the *test* varies (e.g. true or false from variable).
        //
        Init_Logic(
            out,
            VAL_LOGIC(test_cell) == IS_TRUTHY(arg) // vs IS_CONDITIONAL_TRUE()
                and VAL_NUM_QUOTES(test) == VAL_NUM_QUOTES(arg)
        );
        return false;

      case REB_ACTION: {
        DECLARE_LOCAL (arg_specified);
        Derelativize(arg_specified, arg, arg_specifier);
        Dequotify(arg_specified);  // e.g. '':refinement? wants unquoted
        PUSH_GC_GUARD(arg_specified);

        DECLARE_LOCAL (temp);  // test is in `out`
        bool threw = RunQ_Throws(
            temp,
            true,  // `fully` (ensure argument consumed)
            rebU(SPECIFIC(test)),
            NULLIFY_NULLED(arg_specified),  // nulled cells to nullptr for API
            rebEND
        );

        DROP_GC_GUARD(arg_specified);
        if (threw) {
            Move_Value(out, temp);
            return true;
        }

        Init_Logic(out, IS_TRUTHY(temp));  // errors on VOID!
        return false; }

      case REB_DATATYPE:
        Init_Logic(
            out,
            VAL_TYPE_KIND(test_cell) == CELL_KIND(arg_cell)
                and VAL_NUM_QUOTES(arg) == sum_quotes
        );
        return false;

      case REB_TYPESET:
        Init_Logic(
            out,
            TYPE_CHECK(test_cell, CELL_KIND(arg_cell))
                and VAL_NUM_QUOTES(arg) == sum_quotes
        );
        return false;

     case REB_TAG: {  // just support <opt> for now
        bool strict = false;
        Init_Logic(
            out,
            CELL_KIND(arg_cell) == REB_NULL
            and 0 == CT_String(test_cell, Root_Opt_Tag, strict)
            and VAL_NUM_QUOTES(test) == VAL_NUM_QUOTES(arg)
        );
        return false; }

      case REB_INTEGER: // interpret as length
        Init_Logic(
            out,
            ANY_SERIES_KIND(CELL_KIND(arg_cell))
                and VAL_LEN_AT(arg_cell) == VAL_UINT32(test_cell)
                and VAL_NUM_QUOTES(test) == VAL_NUM_QUOTES(arg)
        );
        return false;

      case REB_SYM_WORD: {
        if (Matches_Fake_Type_Constraint(
            arg,
            cast(enum Reb_Symbol, VAL_WORD_SYM(test_cell))
        )){
            Init_True(out);
            return false;
        }
        Init_False(out);
        return false; }

      default:
        break;
    }

    fail (Error_Invalid_Type(test_kind));
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
        arg_specifier,
        0 // number of quotes to add in, start at zero
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
//          [<opt> any-value!]
//      :branch [any-branch!]
//  ]
//
REBNATIVE(else)  // see `tweak :else #defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ELSE;

    if (not Is_Light_Nulled(ARG(optional)))
        RETURN (ARG(optional));

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), NULLED_CELL))
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
//          [<opt> any-value!]
//  ]
//
REBNATIVE(else_q)
{
    INCLUDE_PARAMS_OF_ELSE_Q;
    return Init_Logic(D_OUT, Is_Light_Nulled(ARG(optional)));
}


//
//  then: enfix native [
//
//  {If input is null, return null, otherwise evaluate the branch}
//
//      return: "null if input is null, or branch result (voided if null)"
//          [<opt> any-value!]
//      optional "<deferred argument> Run branch if this is not null"
//          [<opt> any-value!]
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [any-branch!]
//  ]
//
REBNATIVE(then)  // see `tweak :then #defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_THEN;

    if (Is_Light_Nulled(ARG(optional)))
        return nullptr;  // left didn't run, so signal THEN didn't run either

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(optional)))
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
//          [<opt> any-value!]
//  ]
//
REBNATIVE(then_q)
{
    INCLUDE_PARAMS_OF_THEN_Q;
    return Init_Logic(D_OUT, not Is_Light_Nulled(ARG(optional)));
}


//
//  also: enfix native [
//
//  {For non-null input, evaluate and discard branch (like a pass-thru THEN)}
//
//      return: "The same value as input, regardless of if branch runs"
//          [<opt> any-value!]
//      optional "<deferred argument> Run branch if this is not null"
//          [<opt> any-value!]
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [any-branch!]
//  ]
//
REBNATIVE(also)  // see `tweak :also #defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ALSO;  // `then func [x] [(...) :x]` => `also [...]`

    if (Is_Light_Nulled(ARG(optional)))
        return nullptr;  // telegraph original input, but don't run

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(optional)))
        return R_THROWN;

    RETURN (ARG(optional));  // ran, but pass thru the original input
}


//
//  either-match: native [
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
//              quoted!  ; same test, but make quote level part of the test
//          ]
//      value [<opt> any-value!]
//      :branch "Branch to run on non-matches, passed VALUE if ACTION!"
//          [any-branch!]
//      /not "Invert the result of the the test (used by NON)"
//  ]
//
REBNATIVE(either_match)
{
    INCLUDE_PARAMS_OF_EITHER_MATCH;

    if (Match_Core_Throws(D_OUT, ARG(test), SPECIFIED, ARG(value), SPECIFIED))
        return R_THROWN;

    if (
        (not REF(_not_) and VAL_LOGIC(D_OUT))
        or (REF(_not_) and not VAL_LOGIC(D_OUT)
    )){
        RETURN (ARG(value));
    }

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(value)))
        return R_THROWN;

    return D_OUT;
}


//
//  match: native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input if it matched, otherwise null (void if falsey match)"
//          [<opt> any-value!]
//      'test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              word!  ; GET to find actual test
//              action! get-word! get-path!  ; arity-1 filter function
//              path!  ; AND'd tests
//              block!  ; OR'd tests
//              datatype! typeset!  ; literals accepted
//              logic!  ; tests TO-LOGIC compatibility
//              tag!  ; just <opt> for now
//              integer!  ; matches length of series
//              quoted!  ; same test, but make quote level part of the test
//          ]
//      :args [<opt> any-value! <variadic>]
//  ]
//
REBNATIVE(match)
//
// MATCH implements a special frame making behavior, to accomplish:
//
//     >> match parse "aaa" [some "a"]
//     == "AAA"
//
// To do this, it builds a frame for the function, steals its argument, and
// returns it.  Hence it has to be variadic.  EITHER-MATCH provides a more
// easily reusable variant of the MATCH logic (e.g. specialized by ENSURE)
//
{
    INCLUDE_PARAMS_OF_MATCH;

    // !!! The vararg's frame is not really a parent, but try to stay
    // consistent with the naming in subframe code copy/pasted for now...
    //
    REBFRM *parent;
    if (not Is_Frame_Style_Varargs_May_Fail(&parent, ARG(args)))
        fail (
            "Currently MATCH on a VARARGS! only works with a varargs"
            " which is tied to an existing, running frame--not one that is"
            " being simulated from a BLOCK! (e.g. MAKE VARARGS! [...])"
        );

    assert(Is_Action_Frame(parent));

    REBVAL *test = ARG(test);

    switch (KIND3Q_BYTE(test)) {
      case REB_WORD:
      case REB_PATH: {
        if (NOT_CELL_FLAG(test, UNEVALUATED)) // soft quote eval'd
            goto either_match; // allow `MATCH ('NULL?) ...`

        // REBFRM whose built FRAME! context we will steal
        //
        DECLARE_FRAME (f, parent->feed, EVAL_MASK_DEFAULT);  // capture DSP
        Push_Frame(D_OUT, f);

        if (Get_If_Word_Or_Path_Throws(
            D_OUT,
            test,
            SPECIFIED,
            true  // push_refinements, DECLARE_FRAME() captured original DSP
        )){
            Drop_Frame(f);
            return R_THROWN;
        }

        Move_Value(test, D_OUT);

        if (not IS_ACTION(test)) {
            Drop_Frame(f);
            if (
                IS_WORD(test) or IS_GET_WORD(test) or IS_SET_WORD(test)
                or ANY_PATH(test)  // ^-- we allow ISSUE!
            ){
                fail (PAR(test)); // disallow `X: 'Y | MATCH X ...`
            }
            goto either_match; // will typecheck the result
        }

        // It was a non-soft quote eval'd word, the kind we want to give the
        // "magical" functionality to.
        //
        // We run the testing function in place in a way that appears "normal"
        // but actually captures its first argument.  That will be MATCH's
        // return value if the filter function returns a truthy result.

        REBVAL *first_arg;
        if (Make_Invocation_Frame_Throws(f, &first_arg, test)) {
            Drop_Frame(f);
            return R_THROWN;
        }

        if (not first_arg)
            fail ("MATCH with a function pattern must take at least 1 arg");

        Move_Value(D_OUT, first_arg); // steal first argument before call

        DECLARE_LOCAL (temp);
        f->out = SET_END(temp);

        f->rootvar = CTX_ROOTVAR(CTX(f->varlist));
        f->param = ACT_PARAMS_HEAD(VAL_ACTION(test));
        f->arg = f->rootvar + 1;
        f->special = f->arg;

        f->flags.bits = EVAL_MASK_DEFAULT
            | EVAL_FLAG_FULLY_SPECIALIZED
            | FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING);

        Begin_Prefix_Action(f, VAL_ACTION_LABEL(test));

        bool threw = Process_Action_Throws(f);

        Drop_Frame(f);

        if (threw)
            return R_THROWN;

        if (IS_VOID(temp))
            fail (Error_Void_Conditional_Raw());

        // We still have the first argument from the filter call in D_OUT.

        // MATCH *wants* to pass through the argument on a match, but
        // won't do so if the argument was falsey, as that is misleading.
        // Instead it passes a VOID! back (test with `value?` or `null?`)

        if (IS_TRUTHY(temp)) {
            if (IS_VOID(D_OUT))
                return D_OUT;  // don't corrupt, so you can MATCH exact voids
            if (IS_FALSEY(D_OUT))
                return Init_Void(D_OUT, SYM_MATCHED);
            return D_OUT;
        }

        return nullptr; }

      default:
        break;
    }

  either_match:;

    // For the "non-magic" cases that are handled by plain EITHER-TEST, call
    // through with the transformed test.  Just take one normal arg via
    // variadic.

    if (Do_Vararg_Op_Maybe_End_Throws_Core(
        D_OUT,
        VARARG_OP_TAKE,
        ARG(args),
        REB_P_NORMAL
    )){
        return R_THROWN;
    }

    if (IS_END(D_OUT))
        fail ("Frame hack is written to need argument!");

    DECLARE_LOCAL (temp);
    if (Match_Core_Throws(temp, test, SPECIFIED, D_OUT, SPECIFIED))
        return R_THROWN;

    if (VAL_LOGIC(temp)) {
        if (IS_VOID(D_OUT))
            return D_OUT;  // don't corrupt, so you can MATCH exact voids
        if (IS_FALSEY(D_OUT)) // see above for why false match not passed thru
            return Init_Void(D_OUT, SYM_MATCHED);
        return D_OUT;
    }

    return nullptr;
}


//
//  matches: enfix native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input if it matched, otherwise null (void if falsey match)"
//          [<opt> any-value!]
//       value [<opt> any-value!]
//      'test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              word!  ; GET to find actual test
//              action! get-word! get-path!  ; arity-1 filter function
//              path!  ; AND'd tests
//              block!  ; OR'd tests
//              datatype! typeset!  ; literals accepted
//              logic!  ; tests TO-LOGIC compatibility
//              tag!  ; just <opt> for now
//              integer!  ; matches length of series
//              quoted!  ; same test, but make quote level part of the test
//          ]
//  ]
//
REBNATIVE(matches)
{
    INCLUDE_PARAMS_OF_MATCHES;

    if (Match_Core_Throws(D_OUT, ARG(test), SPECIFIED, ARG(value), SPECIFIED))
        return R_THROWN;

    assert(IS_LOGIC(D_OUT));
    return D_OUT;
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

    Init_Nulled(D_OUT);  // so `all []` sees stale falsey value, returns null

    do {
        if (Eval_Step_Maybe_Stale_Throws(D_OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }
        if (GET_CELL_FLAG(D_OUT, OUT_MARKED_STALE)) {
            if (IS_END(f->feed->value))  // `all []`
                break;
            continue;  // `all [comment "hi" 1]`, first step is stale
        }

        if (IS_NULLED(predicate)) {  // default predicate effectively .DID
            if (IS_FALSEY(D_OUT)) {  // false/blank/null triggers failure
                Abort_Frame(f);
                return nullptr;
            }
        }
        else {
            if (RunQ_Throws(
                D_SPARE,
                true,
                rebINLINE(predicate),
                NULLIFY_NULLED(D_OUT),
                rebEND
            )){
                return R_THROWN;
            }

            if (IS_FALSEY(D_SPARE)) {
                Abort_Frame(f);
                return nullptr;
            }
        }
    } while (NOT_END(f->feed->value));

    Drop_Frame(f);

    if (IS_NULLED(D_OUT)) {
        if (NOT_CELL_FLAG(D_OUT, OUT_MARKED_STALE)) {
            //
            // The only way a NULL evaluation that isn't the initial loaded
            // NULL should make it to the end is if a predicate passed it,
            // so we voidify it for: `all .not [null] then [<runs>]`
            //
            assert(not IS_NULLED(predicate));
            return Init_Heavy_Nulled(D_OUT);
        }
    }

    CLEAR_CELL_FLAG(D_OUT, OUT_MARKED_STALE);  // `all [true elide 1 + 2]`

    return D_OUT;  // successful ALL when the last D_OUT assignment passed
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

    Init_Nulled(D_OUT);  // preload output with falsey value

    do {
        if (Eval_Step_Maybe_Stale_Throws(D_OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }
        if (GET_CELL_FLAG(D_OUT, OUT_MARKED_STALE)) {
            if (IS_END(f->feed->value))  // `any []`
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
            if (RunQ_Throws(
                D_SPARE,
                true,
                rebINLINE(predicate),
                NULLIFY_NULLED(D_OUT),
                rebEND
            )){
                return R_THROWN;
            }

            if (IS_TRUTHY(D_SPARE)) {
                Isotopify_If_Nulled(D_OUT);  // `any .not [null] then [<run>]`
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
            CLEAR_CELL_FLAG(D_OUT, OUT_MARKED_STALE);
            goto reached_end;
        }

        if (GET_CELL_FLAG(D_OUT, OUT_MARKED_STALE))
            continue;  // a COMMENT, but not at end.

        bool matched;
        if (IS_NULLED(predicate)) {
            matched = IS_TRUTHY(D_OUT);
        }
        else {
            DECLARE_LOCAL (temp);
            if (RunQ_Throws(
                temp,
                true,  // fully = true (e.g. argument must be taken)
                rebINLINE(predicate),
                D_OUT,  // argument
                rebEND
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
                Move_Value(D_OUT, D_SPARE);
                goto threw;
            }
            Move_Value(ARG(branch), D_SPARE);
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

        if (Do_Branch_With_Throws(D_SPARE, ARG(branch), D_OUT)) {
            Move_Value(D_OUT, D_SPARE);
            goto threw;
        }
        Move_Value(D_OUT, D_SPARE);

        if (not REF(all)) {
            Drop_Frame(f);
            return D_OUT;
        }

        Move_Value(ARG(last), D_OUT);
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
            if (RunQ_Throws(
                temp,
                true,  // fully = true (e.g. both arguments must be taken)
                rebINLINE(predicate),
                left,  // first arg (left hand side if infix)
                D_OUT,  // second arg (right hand side if infix)
                rebEND
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

            if (IS_BLOCK(f_value) or IS_SYM_BLOCK(f_value)) {
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
                if (RunQ_Throws(
                    temp,
                    false,  // fully = false, e.g. arity-0 functions are ok
                    rebU(SPECIFIC(f_value)),  // actions don't need specifiers
                    D_OUT,
                    rebEND
                )){
                    Move_Value(D_OUT, temp);
                    goto threw;
                }
                Move_Value(D_OUT, temp);
                break;
            }

            Fetch_Next_Forget_Lookback(f);
        }

        if (not REF(all)) {
            Drop_Frame(f);
            return D_OUT;
        }

        Move_Value(ARG(last), D_OUT);  // save in case no fallout
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
//          [set-word! set-path!]  ; to left of DEFAULT
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
        Move_Value(D_OUT, Lookup_Word_May_Fail(target, SPECIFIED));
    else {
        assert(IS_SET_PATH(target));

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
            RELVAL *dest = STABLE(ARR_HEAD(composed));
            REBSPC *specifier = VAL_SPECIFIER(target);
            for (i = 0; i < len; ++i, ++dest) {
                const RELVAL *item = VAL_SEQUENCE_AT(D_SPARE, target, i);

                if (not IS_GROUP(item))
                    Derelativize(dest, CELL_TO_VAL(item), VAL_SPECIFIER(target));
                else {
                    if (Do_Any_Array_At_Throws(D_OUT, item, specifier))
                        return R_THROWN;
                    Move_Value(dest, D_OUT);
                }
            }
            TERM_ARRAY_LEN(composed, len);
            Freeze_Array_Shallow(composed);
            Force_Array_Managed(composed);

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
            NULL, // not requesting value to set means it's a get
            EVAL_MASK_DEFAULT
                | EVAL_FLAG_PATH_HARD_QUOTE // pre-COMPOSE'd, GROUP!s literal
        )){
            panic (D_OUT); // shouldn't be possible... no executions!
        }
    }

    if (not IS_NULLED_OR_VOID(D_OUT)) {
        if (not REF(predicate)) {  // no custom additional constraint
            if (not IS_BLANK(D_OUT))  // acts as `x: default .not.blank? [...]`
                return D_OUT;  // count it as "already set"
        }
        else {
            if (rebDid(rebINLINE(predicate), rebQ(D_OUT), rebEND))
                return D_OUT;
        }
    }

    if (Do_Branch_Throws(D_OUT, ARG(branch)))
        return R_THROWN;

    if (IS_SET_WORD(target))
        Move_Value(Sink_Word_May_Fail(target, SPECIFIED), D_OUT);
    else {
        assert(IS_SET_PATH(target));
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
            rebElide(
                NATIVE_VAL(set), rebQ(REF(result)), rebQ(D_OUT),
            rebEND);

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

            unstable const RELVAL *candidate = VAL_ARRAY_AT(ARG(name));
            for (; NOT_END(candidate); candidate++) {
                //
                // !!! Should we test a typeset for illegal name types?
                //
                if (IS_BLOCK(candidate))
                    fail (PAR(name));

                Derelativize(temp1, candidate, VAL_SPECIFIER(ARG(name)));
                Move_Value(temp2, label);

                // Return the THROW/NAME's arg if the names match
                //
                bool strict = false;  // e.g. EQUAL?, better if STRICT-EQUAL?
                if (0 == Compare_Modify_Values(temp1, temp2, strict))
                    goto was_caught;
            }
        }
        else {
            Move_Value(temp1, ARG(name));
            Move_Value(temp2, label);

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

        Move_Value(ARR_AT(a, 0), label); // throw name
        CATCH_THROWN(ARR_AT(a, 1), D_OUT); // thrown value--may be null!
        if (IS_NULLED(ARR_AT(a, 1)))
            TERM_ARRAY_LEN(a, 1); // trim out null value (illegal in block)
        else
            TERM_ARRAY_LEN(a, 2);
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
