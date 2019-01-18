//
//  File: %n-control.c
//  Summary: "native functions for control flow"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
//   return a VOID! result.  Voids are neither true nor false, and since they
//   are values (unlike NULL), they distinguish the signal of a branch that
//   evaluated to NULL from no branch at all.
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
//      'branch "If arity-1 ACTION!, receives the evaluated condition"
//          [block! action! quoted!]
//  ]
//
REBNATIVE(if)
{
    INCLUDE_PARAMS_OF_IF;

    if (IS_CONDITIONAL_FALSE(ARG(condition))) // fails on void, literal blocks
        return nullptr;

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(condition)))
        return R_THROWN;

    return Voidify_If_Nulled(D_OUT); // null means no branch (cues ELSE, etc.)
}


//
//  if-not: native [
//
//  {When TO LOGIC! CONDITION is false, execute branch}
//
//      return: "null if branch not run, otherwise branch result"
//          [<opt> any-value!]
//      condition [<opt> any-value!]
//      'branch [block! action! quoted!]
//  ]
//
REBNATIVE(if_not)
{
    INCLUDE_PARAMS_OF_IF_NOT;

    if (IS_CONDITIONAL_TRUE(ARG(condition))) // fails on void, literal blocks
        return nullptr;

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(condition)))
        return R_THROWN;

    return Voidify_If_Nulled(D_OUT); // null means no branch (cues ELSE, etc.)
}


//
//  either: native [
//
//  {Choose a branch to execute, based on TO-LOGIC of the CONDITION value}
//
//      return: [<opt> any-value!]
//          "Returns null if either branch returns null (unlike IF...ELSE)"
//      condition [<opt> any-value!]
//      'true-branch "If arity-1 ACTION!, receives the evaluated condition"
//          [block! action! quoted!]
//      'false-branch [block! action! quoted!]
//  ]
//
REBNATIVE(either)
{
    INCLUDE_PARAMS_OF_EITHER;

    if (Do_Branch_With_Throws(
        D_OUT,
        IS_CONDITIONAL_TRUE(ARG(condition)) // fails on void, literal blocks
            ? ARG(true_branch)
            : ARG(false_branch),
        ARG(condition)
    )){
        return R_THROWN;
    }

    return D_OUT;
}


inline static bool Single_Test_Throws(
    REBVAL *out, // GC-safe output cell
    const RELVAL *test,
    REBSPC *test_specifier,
    const RELVAL *arg,
    REBSPC *arg_specifier,
    REBCNT sum_quotes
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

    const REBCEL *test_cell = VAL_UNESCAPED(test);
    const REBCEL *arg_cell = VAL_UNESCAPED(arg);

    enum Reb_Kind test_kind = CELL_KIND(test_cell);

    // If test is a WORD! or PATH! then GET it.  To help keep things clear,
    // require GET-WORD! or GET-PATH! for actions to convey they are not being
    // invoked inline, and disallow them on non-actions to help discern them
    // (maybe relax that later)
    //
    //    maybe [integer! :even?] 4 ;-- this is ok
    //    maybe [:integer! even?] 4 ;-- this is not
    //
    if (
        test_kind == REB_WORD
        or test_kind == REB_GET_WORD or test_kind == REB_GET_PATH
    ){
        const bool push_refinements = false;

        DECLARE_LOCAL (dequoted_test); // wouldn't need if Get took any escape
        Dequotify(Derelativize(dequoted_test, test, test_specifier));

        REBSTR *opt_label = NULL;
        REBDSP lowest_ordered_dsp = DSP;
        if (Get_If_Word_Or_Path_Throws( // !!! take any escape level?
            out,
            &opt_label,
            dequoted_test,
            SPECIFIED,
            push_refinements // !!! Look into pushing e.g. `match :foo?/bar x`
        )){
            return true;
        }

        assert(lowest_ordered_dsp == DSP); // would have made specialization
        UNUSED(lowest_ordered_dsp);

        if (IS_NULLED(out))
            fail (Error_No_Value_Raw(dequoted_test));

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
      case REB_PATH: { // AND the tests together
        RELVAL *item = VAL_ARRAY_AT(test_cell);
        REBSPC *specifier = Derive_Specifier(test_specifier, test);

        for (; NOT_END(item); ++item) {
            if (IS_GET_WORD(VAL_UNESCAPED(item)))
                fail ("GET-WORD! may be slated to be illegal in PATH!s");

            if (IS_QUOTED(item))
                fail ("QUOTED! items may be slated to be illegal in PATH!s");

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

            if (not VAL_LOGIC(out)) // any ANDing failing skips block
                return false; // false=no throw
        }
        assert(VAL_LOGIC(out)); // if all tests succeeded in block
        return false; } // return the LOGIC! truth, false=no throw

      case REB_BLOCK: { // OR the tests together
        RELVAL *item = VAL_ARRAY_AT(test_cell);
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
        Dequotify(arg_specified); // e.g. '':refinement? wants unquoted
        PUSH_GC_GUARD(arg_specified);

        bool threw = Apply_Only_Throws(
            out,
            true, // `fully` (ensure argument consumed)
            KNOWN(test),
            NULLIFY_NULLED(arg_specified), // nulled cells to nullptr for API
            rebEND
        );

        DROP_GC_GUARD(arg_specified);
        if (threw)
            return true;

        Init_Logic(out, IS_TRUTHY(out)); // errors on VOID!
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

     case REB_TAG: // just support <opt> for now
        Init_Logic(
            out,
            CELL_KIND(arg_cell) == REB_MAX_NULLED
            and 0 == Compare_String_Vals(test_cell, Root_Opt_Tag, true)
            and VAL_NUM_QUOTES(test) == VAL_NUM_QUOTES(arg)
        );
        return false;

      case REB_INTEGER: // interpret as length
        Init_Logic(
            out,
            ANY_SERIES_KIND(CELL_KIND(arg_cell))
                and VAL_LEN_AT(arg_cell) == VAL_UINT32(test_cell)
                and VAL_NUM_QUOTES(test) == VAL_NUM_QUOTES(arg)
        );
        return false;

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
//      optional "Run branch if this is null"
//          [<opt> any-value!]
//      'branch [block! action! quoted!]
//  ]
//
REBNATIVE(else)
{
    INCLUDE_PARAMS_OF_ELSE; // faster than EITHER-TEST specialized w/`VALUE?`

    if (not IS_NULLED(ARG(optional))) // Note: VOID!s are crucially non-NULL
        RETURN (ARG(optional));

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), NULLED_CELL))
        return R_THROWN;

    return D_OUT; // don't voidify, allows chaining: `else [...] then [...]`
}


//
//  then: enfix native [
//
//  {If input is null, return null, otherwise evaluate the branch}
//
//      return: "null if input is null, or branch result (voided if null)"
//          [<opt> any-value!]
//      optional "Run branch if this is not null"
//          [<opt> any-value!]
//      'branch "If arity-1 ACTION!, receives value that triggered branch"
//          [block! action! quoted!]
//  ]
//
REBNATIVE(then)
{
    INCLUDE_PARAMS_OF_THEN; // faster than EITHER-TEST specialized w/`NULL?`

    if (IS_NULLED(ARG(optional))) // Note: VOID!s are crucially non-NULL
        return nullptr; // left didn't run, so signal THEN didn't run either

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(optional)))
        return R_THROWN;

    return Voidify_If_Nulled(D_OUT); // if left ran, make THEN signal it did
}


//
//  also: enfix native [
//
//  {For non-null input, evaluate and discard branch (like a pass-thru THEN)}
//
//      return: "The same value as input, regardless of if branch runs"
//          [<opt> any-value!]
//      optional "Run branch if this is not null"
//          [<opt> any-value!]
//      'branch "If arity-1 ACTION!, receives value that triggered branch"
//          [block! action! quoted!]
//  ]
//
REBNATIVE(also)
{
    INCLUDE_PARAMS_OF_ALSO; // `then func [x] [(...) :x]` => `also [...]`

    if (IS_NULLED(ARG(optional))) // Note: VOID!s are crucially non-NULL
        return nullptr;

    if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(optional)))
        return R_THROWN;

    RETURN (ARG(optional)); // just passing thru the input
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
//              word! ;-- GET to find actual test
//              action! get-word! get-path! ;-- arity-1 filter function
//              path! ;-- AND'd tests
//              block! ;-- OR'd tests
//              datatype! typeset! ;-- literals accepted
//              logic! ;-- tests TO-LOGIC compatibility
//              tag! ;-- just <opt> for now
//              integer! ;-- matches length of series
//              quoted! ;-- same test, but make quote level part of the test
//          ]
//      value [<opt> any-value!]
//      /else "Instead of returning null on non-matches, run a branch"
//      'branch [block! action! quoted!]
//  ]
//
REBNATIVE(match)
{
    INCLUDE_PARAMS_OF_MATCH;

    if (Match_Core_Throws(D_OUT, ARG(test), SPECIFIED, ARG(value), SPECIFIED))
        return R_THROWN;

    if (VAL_LOGIC(D_OUT)) {
        if (IS_FALSEY(ARG(value)) and not REF(else)) {
            //
            // Don't want `if match [blank! ...] whatever [...]` to fail to
            // run the branch silently, despite match succeeding.  Caller
            // must consciously use /ELSE for that case.
            //
            return Init_Void(D_OUT);
        }
        RETURN (ARG(value));
    }

    if (REF(else)) {
        if (Do_Branch_With_Throws(D_OUT, ARG(branch), ARG(value)))
            return R_THROWN;
        return D_OUT;
    }

    return nullptr;
}


//
//  match2: native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input if it matched, otherwise null (void if falsey match)"
//          [<opt> any-value!]
//      'test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              word! path! ;- special "first-arg-stealing" magic
//              datatype! typeset! block! logic! action! ;-- like EITHER-TEST
//              quoted! ;-- same test, but make quote level part of the test
//          ]
//      :args [any-value! <...>]
//  ]
//
REBNATIVE(match2)
//
// !!! This experimental MATCH variant implemented a special frame making
// `MATCH PARSE "AAA" [SOME "A"]` -> "AAA" behavior.  It would build a frame
// for the function, steal its argument, and return that.  Because this had
// to be variadic, it was a bit of a problem in its interaction with enfix:
//
// https://github.com/metaeducation/ren-c/issues/820
//
// It has been moved aside for the moment until that issue is worked out,
// because it was interfering with the usability of MATCH.
{
    INCLUDE_PARAMS_OF_MATCH2;

    REBVAL *test = ARG(test);

    switch (KIND_BYTE(test)) {
      case REB_WORD:
      case REB_PATH: {
        if (NOT_VAL_FLAG(test, VALUE_FLAG_UNEVALUATED)) // soft quote eval'd
            goto either_test; // allow `MATCH ('NULL?) ...`

        REBSTR *opt_label = NULL;
        REBDSP lowest_ordered_dsp = DSP;
        if (Get_If_Word_Or_Path_Throws(
            D_OUT,
            &opt_label,
            test,
            SPECIFIED,
            true // push_refinements
        )){
            return R_THROWN;
        }

        Move_Value(test, D_OUT);

        if (not IS_ACTION(test)) {
            if (ANY_WORD(test) or ANY_PATH(test))
                fail (Error_Invalid(test)); // disallow `X: 'Y | MATCH X ...`
            goto either_test; // will typecheck the result
        }

        // It was a non-soft quote eval'd word, the kind we want to give the
        // "magical" functionality to.
        //
        // We run the testing function in place in a way that appears "normal"
        // but actually captures its first argument.  That will be MATCH's
        // return value if the filter function returns a truthy result.

        DECLARE_FRAME (f); // REBFRM whose built FRAME! context we will steal

        REBVAL *first_arg;
        if (Make_Invocation_Frame_Throws(
            D_OUT,
            f,
            &first_arg,
            test,
            ARG(args),
            lowest_ordered_dsp
        )){
            return R_THROWN;
        }

        if (not first_arg)
            fail ("MATCH with a function pattern must take at least 1 arg");

        Move_Value(D_OUT, first_arg); // steal first argument before call

        DECLARE_LOCAL (temp);
        f->out = SET_END(temp);

        f->rootvar = CTX_ARCHETYPE(CTX(f->varlist));
        f->param = ACT_PARAMS_HEAD(VAL_ACTION(test));
        f->arg = f->rootvar + 1;
        f->special = f->arg;

        f->flags.bits = (DO_MASK_DEFAULT & ~DO_FLAG_CONST)
            | DO_FLAG_FULLY_SPECIALIZED
            | DO_FLAG_PROCESS_ACTION;

        Begin_Action(f, opt_label);

        bool threw = (*PG_Eval_Throws)(f);

        Drop_Frame(f);

        if (threw)
            return R_THROWN;

        assert(IS_END(f->value)); // we started at END_FLAG, can only throw

        if (IS_VOID(temp))
            fail (Error_Void_Conditional_Raw());

        // We still have the first argument from the filter call in D_OUT.

        // MATCH *wants* to pass through the argument on a match, but
        // won't do so if the argument was falsey, as that is misleading.
        // Instead it passes a VOID! back (test with `value?` or `null?`)

        if (IS_TRUTHY(temp)) {
            if (IS_FALSEY(D_OUT))
                return Init_Void(D_OUT);
            return D_OUT;
        }

        return nullptr; }

      default:
        break;
    }

either_test:;

    // For the "non-magic" cases that are handled by plain EITHER-TEST, call
    // through with the transformed test.  Just take one normal arg via
    // variadic.

    REBVAL *varpar = PAR(args);

    // !!! Hard-quoted arguments don't accept nulls, but we're tweaking the
    // parameter class... make it allow NULL too.
    //
    VAL_TYPESET_BITS(varpar) |= FLAGIT_KIND(REB_MAX_NULLED);
    mutable_KIND_BYTE(varpar) = REB_P_NORMAL; // !!! hack

    if (Do_Vararg_Op_Maybe_End_Throws(D_OUT, ARG(args), VARARG_OP_TAKE))
        return R_THROWN;

    if (IS_END(D_OUT))
        fail ("Frame hack is written to need argument!");

    mutable_KIND_BYTE(varpar) = REB_P_HARD_QUOTE;
    VAL_TYPESET_BITS(varpar) &= ~FLAGIT_KIND(REB_MAX_NULLED);

    DECLARE_LOCAL (temp);
    if (Match_Core_Throws(temp, test, SPECIFIED, D_OUT, SPECIFIED))
        return R_THROWN;

    if (VAL_LOGIC(temp)) {
        if (IS_FALSEY(D_OUT)) // see above for why false match not passed thru
            return Init_Void(D_OUT);
        return D_OUT;
    }

    return nullptr;
}


//
//  all: native [
//
//  {Short-circuiting variant of AND, using a block of expressions as input}
//
//      return: "Product of last evaluation if all truthy, else null"
//          [<opt> any-value!]
//      block "Block of expressions"
//          [block!]
//  ]
//
REBNATIVE(all)
{
    INCLUDE_PARAMS_OF_ALL;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    Init_Nulled(D_OUT); // default return result

    while (NOT_END(f->value)) {
        if (Eval_Step_Maybe_Stale_Throws(D_OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_FALSEY(D_OUT)) { // any false/blank/null will trigger failure
            Abort_Frame(f);
            return nullptr;
        }

        // consider case of `all [true elide print "hi"]`
        //
        D_OUT->header.bits &= ~OUT_MARKED_STALE;
    }

    Drop_Frame(f);
    return D_OUT; // successful ALL when the last D_OUT assignment is truthy
}


//
//  any: native [
//
//  {Short-circuiting version of OR, using a block of expressions as input}
//
//      return: "First truthy evaluative result, or null if all falsey"
//          [<opt> any-value!]
//      block "Block of expressions"
//          [block!]
//  ]
//
REBNATIVE(any)
{
    INCLUDE_PARAMS_OF_ANY;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    Init_Nulled(D_OUT); // default return result

    while (NOT_END(f->value)) {
        if (Eval_Step_Maybe_Stale_Throws(D_OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_TRUTHY(D_OUT)) { // successful ANY returns the value
            Abort_Frame(f);
            return D_OUT;
        }

        // consider case of `any [true elide print "hi"]`
        //
        D_OUT->header.bits &= ~OUT_MARKED_STALE;
    }

    Drop_Frame(f);
    return nullptr;
}


//
//  none: native [
//
//  {Short circuiting version of NOR, using a block of expressions as input}
//
//      return: "BAR! if all expressions are falsey, null if any are truthy"
//          [<opt> bar!]
//      block "Block of expressions."
//          [block!]
//  ]
//
REBNATIVE(none)
//
// !!! In order to reduce confusion and accidents in the near term, the
// %mezz-legacy.r renames this to NONE-OF and makes NONE report an error.
{
    INCLUDE_PARAMS_OF_NONE;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    Init_Nulled(D_OUT); // default return result

    while (NOT_END(f->value)) {
        if (Eval_Step_Maybe_Stale_Throws(D_OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_TRUTHY(D_OUT)) { // any true results mean failure
            Abort_Frame(f);
            return nullptr;
        }

        // consider case of `none [true elide print "hi"]`
        //
        D_OUT->header.bits &= ~OUT_MARKED_STALE;
    }

    Drop_Frame(f);
    return Init_Bar(D_OUT); // truthy, but doesn't suggest LOGIC! on failure
}


//
//  case: native [
//
//  {Evaluates each condition, and when true, evaluates what follows it}
//
//      return: [<opt> any-value!]
//          "Last matched case evaluation, or null if no cases matched"
//      cases [block!]
//          "Block of cases (conditions followed by branches)"
//      /all
//          "Evaluate all cases (do not stop at first logically true case)"
//  ]
//
REBNATIVE(case)
{
    INCLUDE_PARAMS_OF_CASE;

    REBVAL *block = ARG(cases);

    DECLARE_FRAME (f);
    Push_Frame(f, block); // array GC safe now, can re-use `block` cell

    Init_Nulled(D_OUT); // default return result

    DECLARE_LOCAL (cell); // unsafe to use ARG() slots as frame's f->out
    SET_END(cell);
    PUSH_GC_GUARD(cell);

    while (NOT_END(f->value)) {

        // Perform 1 EVALUATE's worth of evaluation on a "condition" to test
        // Will consume any pending "invisibles" (COMMENT, ELIDE, DUMP...)

        if (Eval_Step_Throws(SET_END(cell), f)) {
            DROP_GC_GUARD(cell);
            Move_Value(D_OUT, cell);
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_END(cell)) {
            assert(IS_END(f->value));
            break;
        }

        // The last condition will "fall out" if there is no branch/choice:
        //
        //     case [1 > 2 [...] 3 > 4 [...] 10 + 20] = 30
        //     choose [1 > 2 (literal group) 3 > 4 <tag> 10 + 20] = 30
        //
        if (IS_END(f->value)) {
            DROP_GC_GUARD(cell);
            Drop_Frame(f);
            return Move_Value(D_OUT, cell);
        }

        if (IS_CONDITIONAL_FALSE(cell)) { // not a matching condition
            //
            // Maintain symmetry with IF's typechecking of non-taken branches:
            //
            // >> if false <some-tag>
            // ** Script Error: if does not allow tag! for its branch argument
            //
            if (not (
                IS_BLOCK(f->value)
                or IS_ACTION(f->value)
                or IS_QUOTED(f->value)
                or IS_GROUP(f->value) // don't evaluate this case...
            )){
                fail (Error_Invalid_Core(cell, f->specifier));
            }

            Fetch_Next_In_Frame(nullptr, f); // skip next, whatever it is
            continue;
        }

        // Note: we are preserving `cell` to pass to an arity-1 ACTION!

        if (not IS_GROUP(f->value))
            Derelativize(D_OUT, f->value, f->specifier); // null not possible
        else {
            if (Do_At_Throws(
                D_OUT,
                VAL_ARRAY(f->value),
                VAL_INDEX(f->value),
                f->specifier
            )){
                Abort_Frame(f);
                DROP_GC_GUARD(cell);
                return R_THROWN;
            }
        }

        Fetch_Next_In_Frame(nullptr, f); // keep matching if /ALL

        f->gotten = nullptr; // can't hold onto cache, running user code

        if (IS_QUOTED(D_OUT)) {
            Unquotify(D_OUT, 1);
        }
        else if (IS_BLOCK(D_OUT)) {
            Move_Value(block, D_OUT); // can't evaluate into ARG(block)
            if (Do_Any_Array_At_Throws(D_OUT, block)) {
                Abort_Frame(f);
                DROP_GC_GUARD(cell);
                return R_THROWN;
            }
        }
        else if (IS_ACTION(D_OUT)) {
            Move_Value(block, D_OUT); // can't evaluate into ARG(block)
            if (Do_Branch_With_Throws(D_OUT, block, cell)) {
                Abort_Frame(f);
                DROP_GC_GUARD(cell);
                return R_THROWN;
            }
        } else
            fail (Error_Invalid_Core(D_OUT, f->specifier));

        Voidify_If_Nulled(D_OUT); // null is reserved for no branch taken

        if (not REF(all)) {
            DROP_GC_GUARD(cell);
            Abort_Frame(f);
            return D_OUT;
        }
    }

    DROP_GC_GUARD(cell);
    Drop_Frame(f);
    return D_OUT;
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
//      ; !!! /STRICT may have a different name
//      ; https://forum.rebol.info/t/349
//      /strict "Use STRICT-EQUAL? when comparing cases instead of EQUAL?"
//      ; !!! Is /QUOTE truly needed?
//      /quote "Do not evaluate comparison values"
//      ; !!! Needed in spec for ADAPT to override in shim
//      /default "Deprecated: use fallout feature or ELSE, UNLESS, etc."
//      default-branch [block!]
//  ]
//
REBNATIVE(switch)
{
    INCLUDE_PARAMS_OF_SWITCH;

    if (REF(default))
        fail (
            "SWITCH/DEFAULT is no longer supported by the core.  Use the"
            " DEFAULT [...] as the last clause, or ELSE/UNLESS/!!/etc. based"
            " on null result: https://forum.rebol.info/t/312"
        );
    UNUSED(ARG(default_branch));

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(cases));

    REBVAL *value = ARG(value);

    if (IS_BLOCK(value) and GET_VAL_FLAG(value, VALUE_FLAG_UNEVALUATED))
        fail (Error_Block_Switch_Raw(value)); // `switch [x] [...]` safeguard

    Init_Nulled(D_OUT); // used for "fallout"

    while (NOT_END(f->value)) {
        //
        // If a branch is seen at this point, it doesn't correspond to any
        // condition to match.  If no more tests are run, let it suppress the
        // feature of the last value "falling out" the bottom of the switch
        //
        if (IS_BLOCK(f->value)) {
            Init_Nulled(D_OUT);
            Fetch_Next_In_Frame(nullptr, f);
            continue;
        }

        if (IS_ACTION(f->value)) {
            //
            // It's a literal ACTION!, e.g. one composed in the block:
            //
            //    switch :some-func compose [
            //        :append [print "not this case... this is fine"]
            //        :insert (:branch) ;-- it's this situation
            //    ]
            //
        action_not_supported:
            fail (
                "ACTION! branches currently not supported in SWITCH --"
                " none existed after having the feature for 2 years."
                " Complain if you found a good use for it."
            );
        }

        if (REF(quote))
            Quote_Next_In_Frame(D_OUT, f);
        else {
            if (Eval_Step_Throws(SET_END(D_OUT), f)) {
                Abort_Frame(f);
                return R_THROWN;
            }

            if (IS_END(D_OUT)) {
                assert(IS_END(f->value));
                Init_Nulled(D_OUT);
                break;
            }
        }

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
        // !!! A branch composed into the switch cases block may want to see
        // the un-mutated condition value.

        if (!Compare_Modify_Values(ARG(value), D_OUT, REF(strict) ? 1 : 0))
            continue;

        // Skip ahead to try and find a block, to treat as code for the match

        while (true) {
            if (IS_END(f->value)) {
                Drop_Frame(f);
                return D_OUT; // last test "falls out", might be void
            }
            if (IS_BLOCK(f->value))
                break;
            if (IS_ACTION(f->value))
                goto action_not_supported; // literal action
            Fetch_Next_In_Frame(nullptr, f);
        }

        if (Do_At_Throws( // it's a match, so run the BLOCK!
            D_OUT,
            VAL_ARRAY(f->value),
            VAL_INDEX(f->value),
            f->specifier
        )){
            Abort_Frame(f);
            return R_THROWN;
        }

        Voidify_If_Nulled(D_OUT); // null is reserved for no branch run

        if (not REF(all)) {
            Abort_Frame(f);
            return D_OUT;
        }

        Fetch_Next_In_Frame(nullptr, f); // keep matching if /ALL
    }

    Drop_Frame(f);
    return D_OUT; // last test "falls out" or last match if /ALL, may be void
}


//
//  default: enfix native/body [
//
//  {Set word or path to a default value if it is not set yet or blank.}
//
//      return: "Former value or branch result, can only be null if no target"
//          [<opt> any-value!]
//      :target "Word or path which might be set--no target always branches"
//          [<skip> set-word! set-path!]
//      'branch "If target not set already, this is evaluated and stored there"
//          [block! action! quoted!]
//      :look "Variadic lookahead used to make sure at end if no target"
//          [<...>]
//      /only "Consider target being BLANK! to be a value not to overwrite"
//  ][
//      if unset? 'target [ ;-- `case [... default [...]]`
//          if not tail? look [
//              fail ["DEFAULT usage with no left hand side must be at <end>"]
//          ]
//          return do :branch
//      ]
//      if set-path? target [target: compose target]
//      either all [
//          value? set* lit gotten: get/hard target
//          only or [not blank? :gotten]
//      ][
//          :gotten ;; so that `x: y: default z` leads to `x = y`
//      ][
//          set/hard target do :branch
//      ]
//  ]
//
REBNATIVE(default)
{
    INCLUDE_PARAMS_OF_DEFAULT;

    REBVAL *target = ARG(target);

    if (IS_NULLED(target)) { // e.g. `case [... default [...]]`
        UNUSED(ARG(look));
        if (NOT_END(frame_->value)) // !!! shortcut using variadic for now
            fail ("DEFAULT usage with no left hand side must be at <end>");

        if (Do_Branch_Throws(D_OUT, ARG(branch)))
            return R_THROWN;

        return D_OUT; // NULL is okay in this case
    }

    if (IS_SET_WORD(target))
        Move_Opt_Var_May_Fail(D_OUT, target, SPECIFIED);
    else {
        assert(IS_SET_PATH(target));

        // We want to be able to default a path with groups in it, but don't
        // want to double-evaluate.  In a userspace DEFAULT we would do
        // COMPOSE on the PATH! and then use GET/HARD and SET/HARD.  To make
        // a faster native we just do a more optimal version of that.
        //
        bool has_groups = false;
        RELVAL *item = VAL_ARRAY_AT(target);
        for (; NOT_END(item); ++item) {
            if (IS_GROUP(item))
                has_groups = true;
        }
        if (has_groups) {
            REBARR *composed = Make_Arr(VAL_LEN_AT(target));
            RELVAL *dest = ARR_HEAD(composed);
            item = VAL_ARRAY_AT(target);
            REBSPC *specifier = VAL_SPECIFIER(target);
            for (; NOT_END(item); ++item, ++dest) {
                if (not IS_GROUP(item))
                    Derelativize(dest, item, VAL_SPECIFIER(target));
                else {
                    // !!! This only does GROUP!s, but if there are GET-WORD!
                    // in the path the group evaluation could have side
                    // effects that change them as they go.  That's a weird
                    // edge case...not going to address it yet, as perhaps
                    // GET-WORD! in paths aren't good anyway.
                    //
                    REBSPC *derived = Derive_Specifier(specifier, item);
                    REBIXO indexor = Eval_Array_At_Core(
                        Init_Void(D_OUT),
                        nullptr,
                        VAL_ARRAY(item),
                        VAL_INDEX(item),
                        derived,
                        (DO_MASK_DEFAULT & ~DO_FLAG_CONST)
                            | DO_FLAG_TO_END
                            | (frame_->flags.bits & DO_FLAG_CONST)
                    );
                    if (indexor == THROWN_FLAG)
                        return R_THROWN;
                    Move_Value(dest, D_OUT);
                }
            }
            TERM_ARRAY_LEN(composed, VAL_LEN_AT(target));
            Init_Any_Array(target, REB_SET_PATH, composed);
        }

        if (Eval_Path_Throws_Core(
            D_OUT,
            NULL, // not requesting symbol means refinements not allowed
            VAL_ARRAY(target),
            VAL_INDEX(target),
            VAL_SPECIFIER(target),
            NULL, // not requesting value to set means it's a get
            DO_FLAG_PATH_HARD_QUOTE // pre-COMPOSE'd, so GROUP!s are literal
        )){
            panic (D_OUT); // shouldn't be possible... no executions!
        }
    }

    if (not IS_NULLED(D_OUT) and (not IS_BLANK(D_OUT) or REF(only)))
        return D_OUT; // count it as "already set" !!! what about VOID! ?

    if (Do_Branch_Throws(D_OUT, ARG(branch)))
        return R_THROWN;

    if (IS_SET_WORD(target))
        Move_Value(Sink_Var_May_Fail(target, SPECIFIED), D_OUT);
    else {
        assert(IS_SET_PATH(target));
        DECLARE_LOCAL (dummy);
        if (Eval_Path_Throws_Core(
            dummy,
            NULL, // not requesting symbol means refinements not allowed
            VAL_ARRAY(target),
            VAL_INDEX(target),
            VAL_SPECIFIER(target),
            D_OUT,
            DO_FLAG_PATH_HARD_QUOTE // path precomposed, no double evaluating
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
//      block "Block to evaluate"
//          [block!]
//      /name "Catches a named throw" ;-- should it be called /named ?
//      names "Names to catch (single name if not block)"
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

    if (not Do_Any_Array_At_Throws(D_OUT, ARG(block)))
        return nullptr; // no throw means just return null

    const REBVAL *label = VAL_THROWN_LABEL(D_OUT);

    if (REF(any) and not (
        IS_ACTION(label)
        and VAL_ACT_DISPATCHER(label) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(quit) and (
        IS_ACTION(label)
        and VAL_ACT_DISPATCHER(label) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(name)) {
        //
        // We use equal? by way of Compare_Modify_Values, and re-use the
        // refinement slots for the mutable space

        REBVAL *temp1 = ARG(quit);
        REBVAL *temp2 = ARG(any);

        if (IS_BLOCK(ARG(names))) {
            //
            // Test all the words in the block for a match to catch

            RELVAL *candidate = VAL_ARRAY_AT(ARG(names));
            for (; NOT_END(candidate); candidate++) {
                //
                // !!! Should we test a typeset for illegal name types?
                //
                if (IS_BLOCK(candidate))
                    fail (Error_Invalid(ARG(names)));

                Derelativize(temp1, candidate, VAL_SPECIFIER(ARG(names)));
                Move_Value(temp2, label);

                // Return the THROW/NAME's arg if the names match
                // !!! 0 means equal?, but strict-equal? might be better
                //
                if (Compare_Modify_Values(temp1, temp2, 0))
                    goto was_caught;
            }
        }
        else {
            Move_Value(temp1, ARG(names));
            Move_Value(temp2, label);

            // Return the THROW/NAME's arg if the names match
            // !!! 0 means equal?, but strict-equal? might be better
            //
            if (Compare_Modify_Values(temp1, temp2, 0))
                goto was_caught;
        }
    }
    else {
        // Return THROW's arg only if it did not have a /NAME supplied
        //
        if (IS_BLANK(label))
            goto was_caught;
    }

    return R_THROWN; // throw name is in D_OUT, value is held task local

  was_caught:

    if (REF(name) or REF(any)) {
        REBARR *a = Make_Arr(2);

        Move_Value(ARR_AT(a, 0), label); // throw name
        CATCH_THROWN(ARR_AT(a, 1), D_OUT); // thrown value--may be null!
        if (IS_NULLED(ARR_AT(a, 1)))
            TERM_ARRAY_LEN(a, 1); // trim out null value (illegal in block)
        else
            TERM_ARRAY_LEN(a, 2);
        return Init_Block(D_OUT, a);
    }

    CATCH_THROWN(D_OUT, D_OUT); // thrown value
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
//      name-value [word! action! object!]
//  ]
//
REBNATIVE(throw)
//
// Choices are currently limited for what one can use as a "name" of a THROW.
// Note blocks as names would conflict with the `name_list` feature in CATCH.
//
// !!! Should parameters be /NAMED and NAME ?
{
    INCLUDE_PARAMS_OF_THROW;

    return Init_Thrown_With_Label(
        D_OUT,
        ARG(value),
        REF(name) ? ARG(name_value) : BLANK_VALUE // uses blank, not null
    );
}
