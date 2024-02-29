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
//   return a trash result.  Trashes are neither true nor false, and since they
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
//      branch "If arity-1 ACTION!, receives the evaluated condition"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(if)
{
    INCLUDE_PARAMS_OF_IF;

    if (IS_CONDITIONAL_FALSE(ARG(condition))) // fails on void, literal blocks
        return nullptr;

    if (Do_Branch_With_Throws(OUT, ARG(branch), ARG(condition)))
        return R_THROWN;

    return Trashify_If_Nulled(OUT);  // trash means no branch (cues ELSE)
}


//
//  if-not: native [
//
//  {When TO LOGIC! CONDITION is false, execute branch}
//
//      return: "null if branch not run, otherwise branch result"
//          [<opt> any-value!]
//      condition [<opt> any-value!]
//      branch [block! action!]
//  ]
//
DECLARE_NATIVE(if_not)
{
    INCLUDE_PARAMS_OF_IF_NOT;

    if (IS_CONDITIONAL_TRUE(ARG(condition))) // fails on void, literal blocks
        return nullptr;

    if (Do_Branch_With_Throws(OUT, ARG(branch), ARG(condition)))
        return R_THROWN;

    return Trashify_If_Nulled(OUT);  // trash means no branch (cues ELSE)
}


//
//  either: native [
//
//  {Choose a branch to execute, based on TO-LOGIC of the CONDITION value}
//
//      return: [<opt> any-value!]
//          "Returns null if either branch returns null (unlike IF...ELSE)"
//      condition [<opt> any-value!]
//      true-branch "If arity-1 ACTION!, receives the evaluated condition"
//          [block! action!]
//      false-branch [block! action!]
//  ]
//
DECLARE_NATIVE(either)
{
    INCLUDE_PARAMS_OF_EITHER;

    if (Do_Branch_With_Throws(
        OUT,
        IS_CONDITIONAL_TRUE(ARG(condition)) // fails on void, literal blocks
            ? ARG(true_branch)
            : ARG(false_branch),
        ARG(condition)
    )){
        return R_THROWN;
    }

    return OUT;
}


//  Either_Test_Core_Throws: C
//
// Note: There was an idea of turning the `test` BLOCK! into some kind of
// dialect.  That was later supplanted by idea of MATCH...which bridges with
// a natural interface to functions like PARSE for providing such dialects.
// This routine is just for basic efficiency behind constructs like ELSE
// that want to avoid frame creation overhead.  So BLOCK! just means typeset.
//
bool Either_Test_Core_Throws(
    Value* out, // GC-safe output cell
    Value* test, // modified
    const Value* arg
){
    switch (VAL_TYPE(test)) {

    case REB_LOGIC: // test for "truthy" or "falsey"
        //
        // If this is the result of composing together a test with a literal,
        // it may be the *test* that changes...so in effect, we could be
        // "testing the test" on a fixed value.  Allow literal blocks (e.g.
        // use IS_TRUTHY() instead of IS_CONDITIONAL_TRUE())
        //
        Init_Logic(out, VAL_LOGIC(test) == IS_TRUTHY(arg));
        return false;

      case REB_WORD:
      case REB_PATH: {
        //
        // !!! Because we do not push refinements here, this means that a
        // specialized action will be generated if the user says something
        // like `either-test 'foo?/bar x [...]`.  It's possible to avoid
        // this by pushing a frame before the Get_If_Word_Or_Path_Throws()
        // and gathering the refinements on the stack, but a bit more work
        // for an uncommon case...revisit later.
        //
        const bool push_refinements = false;

        Symbol* opt_label = nullptr;
        REBDSP lowest_ordered_dsp = DSP;
        if (Get_If_Word_Or_Path_Throws(
            out,
            &opt_label,
            test,
            SPECIFIED,
            push_refinements
        )){
            return true;
        }

        assert(lowest_ordered_dsp == DSP); // would have made specialization
        UNUSED(lowest_ordered_dsp);

        Move_Value(test, out);

        if (not IS_ACTION(test))
            fail ("EITHER-TEST only takes WORD! and PATH! for ACTION! vars");
        goto handle_action; }

      case REB_ACTION: {
      handle_action:;

        if (Apply_Only_Throws(
            out,
            true, // `fully` (ensure argument consumed)
            test,
            NULLIZE(arg), // convert nulled cells to C nullptr for API
            rebEND
        )){
            return true;
        }

        if (IS_TRASH(out))
            fail (Error_Trash_Conditional_Raw());

        Init_Logic(out, IS_TRUTHY(out));
        return false; }

      case REB_DATATYPE:
        Init_Logic(out, VAL_TYPE_KIND(test) == VAL_TYPE(arg));
        return false;

      case REB_TYPESET:
        Init_Logic(out, TYPE_CHECK(test, VAL_TYPE(arg)));
        return false;

    case REB_BLOCK: {
        Cell* item = Cell_Array_At(test);
        if (IS_END(item)) {
            //
            // !!! If the test is just [], what's that?  People aren't likely
            // to write it literally, but COMPOSE/etc. might make it.
            //
            fail ("No tests found BLOCK! passed to EITHER-TEST.");
        }

        REBSPC *specifier = VAL_SPECIFIER(test);
        for (; NOT_END(item); ++item) {
            const Cell* var
                = IS_WORD(item)
                    ? Get_Opt_Var_May_Fail(item, specifier)
                    : item;

            if (IS_DATATYPE(var)) {
                if (VAL_TYPE_KIND(var) == VAL_TYPE(arg)) {
                    Init_True(out);
                    return false;
                }
            }
            else if (IS_TYPESET(var)) {
                if (TYPE_CHECK(var, VAL_TYPE(arg))) {
                    Init_True(out);
                    return false;
                }
            }
            else
                fail (Error_Invalid_Type(VAL_TYPE(var)));
        }
        Init_False(out);
        return false; }

      default:
        break;
    }

    fail (Error_Invalid_Type(VAL_TYPE(arg)));
}


//
//  either-test: native [
//
//  {If argument passes test, return it as-is, otherwise take the branch}
//
//      return: "Input argument if it matched, or branch result"
//          [<opt> any-value!]
//      test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              word! path! action! ;-- arity-1 filter function, opt named
//              datatype! typeset! block! ;-- typeset specification forms
//              logic! ;-- tests TO-LOGIC compatibility
//          ]
//      arg [<opt> any-value!]
//      branch "If arity-1 ACTION!, receives the non-matching argument"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(either_test)
{
    INCLUDE_PARAMS_OF_EITHER_TEST;

    if (Either_Test_Core_Throws(OUT, ARG(test), ARG(arg)))
        return R_THROWN;

    if (VAL_LOGIC(OUT))
        RETURN (ARG(arg));

    if (Do_Branch_With_Throws(OUT, ARG(branch), ARG(arg)))
        return R_THROWN;

    return OUT;
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
//      branch [block! action!]
//  ]
//
DECLARE_NATIVE(else)
{
    INCLUDE_PARAMS_OF_ELSE; // faster than EITHER-TEST specialized w/`VALUE?`

    if (not IS_NULLED(ARG(optional)))  // Note: trash is crucially non-NULL
        RETURN (ARG(optional));

    if (Do_Branch_With_Throws(OUT, ARG(branch), NULLED_CELL))
        return R_THROWN;

    return OUT;  // don't trashify, allows chaining: `else [...] then [...]`
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
//      branch "If arity-1 ACTION!, receives value that triggered branch"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(then)
{
    INCLUDE_PARAMS_OF_THEN; // faster than EITHER-TEST specialized w/`NULL?`

    if (IS_NULLED(ARG(optional)))  // Note: trash is crucially non-NULL
        return nullptr; // left didn't run, so signal THEN didn't run either

    if (Do_Branch_With_Throws(OUT, ARG(branch), ARG(optional)))
        return R_THROWN;

    return Trashify_If_Nulled(OUT);  // if left ran, make THEN signal it did
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
//      branch "If arity-1 ACTION!, receives value that triggered branch"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(also)
{
    INCLUDE_PARAMS_OF_ALSO; // `then func [x] [(...) :x]` => `also [...]`

    if (IS_NULLED(ARG(optional)))  // Note: trash is crucially non-NULL
        return nullptr;

    if (Do_Branch_With_Throws(OUT, ARG(branch), ARG(optional)))
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
//      test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              datatype! typeset! block! logic! action! ;-- like EITHER-TEST
//          ]
//      value [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(match)
{
    INCLUDE_PARAMS_OF_MATCH;

    Value* test = ARG(test);

    Value* value = ARG(value);

    DECLARE_VALUE (temp);
    if (Either_Test_Core_Throws(temp, test, value))
        return R_THROWN;

    if (VAL_LOGIC(temp)) {
        if (IS_FALSEY(value)) // see above for why false match not passed thru
            return Init_Trash(OUT);
        return Move_Value(OUT, value);
    }

    return nullptr;
}


//
//  non: native [
//
//  {Make sure a value does NOT match a type constraint (see also: ENSURE)}
//
//      return: "Input value if it passes the type test"
//          [<opt> any-value!]
//      test "The test to apply (limited to DATATYPE! and NULL at this time)"
//          [<opt> datatype!]
//      value "Value to test (will either be returned as result or error)"
//          [<opt> any-value!]
// ]
//
DECLARE_NATIVE(non)
//
// !!! This is a partial implementation of NON implemented for R3C, just good
// enough for `non [trash!]` and `non null` cases to give validation options to
// those wanting a less permissive SET (now that it has no /ANY refinement).
{
    INCLUDE_PARAMS_OF_NON;

    Value* test = ARG(test);
    Value* value = ARG(value);

    if (IS_NULLED(test)) {  // not a datatype, needs special case
        if (IS_NULLED(value))
            fail ("NON expected value to not be NULL, but it was");
    }
    else if (VAL_TYPE_KIND(test) == REB_TRASH) {  // specialize common case
        if (IS_TRASH(value))
            fail ("NON expected value to not be trash, but it was");
    }
    else if (not TYPE_CHECK(value, VAL_TYPE_KIND(test))) {
        fail ("NON expected value to not match a type, but it did match");
    }

    RETURN (value);
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
DECLARE_NATIVE(all)
{
    INCLUDE_PARAMS_OF_ALL;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    Init_Nulled(OUT); // default return result

    while (NOT_END(f->value)) {
        if (Eval_Step_Maybe_Stale_Throws(OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_FALSEY(OUT)) { // any false/blank/null will trigger failure
            Abort_Frame(f);
            return nullptr;
        }

        // consider case of `all [true elide print "hi"]`
        //
        OUT->header.bits &= ~OUT_MARKED_STALE;
    }

    Drop_Frame(f);
    return OUT; // successful ALL when the last OUT assignment is truthy
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
DECLARE_NATIVE(any)
{
    INCLUDE_PARAMS_OF_ANY;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    Init_Nulled(OUT); // default return result

    while (NOT_END(f->value)) {
        if (Eval_Step_Maybe_Stale_Throws(OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_TRUTHY(OUT)) { // successful ANY returns the value
            Abort_Frame(f);
            return OUT;
        }

        // consider case of `any [true elide print "hi"]`
        //
        OUT->header.bits &= ~OUT_MARKED_STALE;
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
DECLARE_NATIVE(none)
//
// !!! In order to reduce confusion and accidents in the near term, the
// %mezz-legacy.r renames this to NONE-OF and makes NONE report an error.
{
    INCLUDE_PARAMS_OF_NONE;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    Init_Nulled(OUT); // default return result

    while (NOT_END(f->value)) {
        if (Eval_Step_Maybe_Stale_Throws(OUT, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_TRUTHY(OUT)) { // any true results mean failure
            Abort_Frame(f);
            return nullptr;
        }

        // consider case of `none [true elide print "hi"]`
        //
        OUT->header.bits &= ~OUT_MARKED_STALE;
    }

    Drop_Frame(f);
    return Init_Bar(OUT); // truthy, but doesn't suggest LOGIC! on failure
}


// Shared code for CASE (which runs BLOCK! clauses as code) and CHOOSE (which
// returns values as-is, e.g. `choose [true [print "hi"]]` => `[print "hi]`
//
static REB_R Case_Choose_Core_May_Throw(
    REBFRM *frame_,
    bool choose // do not evaluate branches, just "choose" them
){
    INCLUDE_PARAMS_OF_CASE;

    Value* block = ARG(cases); // for CHOOSE, it's "choices" not "cases"

    DECLARE_FRAME (f);
    Push_Frame(f, block); // array GC safe now, can re-use `block` cell

    Init_Nulled(OUT); // default return result

    DECLARE_VALUE (cell); // unsafe to use ARG() slots as frame's f->out
    SET_END(cell);
    PUSH_GC_GUARD(cell);

    while (NOT_END(f->value)) {

        // Perform 1 EVALUATE's worth of evaluation on a "condition" to test
        // Will consume any pending "invisibles" (COMMENT, ELIDE, DUMP...)

        if (Eval_Step_Throws(SET_END(cell), f)) {
            DROP_GC_GUARD(cell);
            Move_Value(OUT, cell);
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_END(cell)) {  // !!! R3C patch for permissive invisibility
            if (IS_END(f->value))
                break;
            continue;
        }

        // The last condition will "fall out" if there is no branch/choice:
        //
        //     case [1 > 2 [...] 3 > 4 [...] 10 + 20] = 30
        //     choose [1 > 2 (literal group) 3 > 4 <tag> 10 + 20] = 30
        //
        if (IS_END(f->value)) {
            DROP_GC_GUARD(cell);
            Drop_Frame(f);
            return Move_Value(OUT, cell);
        }

        if (IS_CONDITIONAL_FALSE(cell)) { // not a matching condition
            if (choose) {
                Fetch_Next_In_Frame(nullptr, f); // skip next, whatever it is
                continue;
            }

            // Even if branch is being skipped, it gets an evaluation--like
            // how `if false (print "A" [print "B"])` prints A, but not B.
            //
            if (Eval_Step_Throws(SET_END(cell), f)) {
                Abort_Frame(f);
                // preserving `out` value (may be previous match)
                DROP_GC_GUARD(cell);
                return Move_Value(OUT, cell);
            }

            if (IS_END(cell))
                continue;  // a COMMENT

            // Maintain symmetry with IF's typechecking of non-taken branches:
            //
            // >> if false <some-tag>
            // ** Script Error: if does not allow tag! for its branch argument
            //
            if (not IS_BLOCK(cell) and not IS_ACTION(cell))
                fail (Error_Invalid_Core(cell, f->specifier));

            continue;
        }

        if (choose) {
            Derelativize(OUT, f->value, f->specifier); // null not possible
            Fetch_Next_In_Frame(nullptr, f); // keep matching if /ALL
        }
        else {
            // Note: we are preserving `cell` to pass to an arity-1 ACTION!

            if (Eval_Step_Throws(SET_END(OUT), f)) {
                DROP_GC_GUARD(cell);
                Abort_Frame(f);
                return R_THROWN;
            }

            f->gotten = nullptr; // can't hold onto cache, running user code

            if (IS_BLOCK(OUT)) {
                if (Do_Any_Array_At_Throws(OUT, OUT)) { // legal args
                    Abort_Frame(f);
                    DROP_GC_GUARD(cell);
                    return R_THROWN;
                }
            }
            else if (IS_ACTION(OUT)) {
                Move_Value(block, OUT); // can't evaluate into ARG(block)
                if (Do_Branch_With_Throws(OUT, block, cell)) {
                    Abort_Frame(f);
                    DROP_GC_GUARD(cell);
                    return R_THROWN;
                }
            } else
                fail (Error_Invalid_Core(OUT, f->specifier));

            Trashify_If_Nulled(OUT);  // null is reserved for no branch taken
        }

        if (not REF(all)) {
            DROP_GC_GUARD(cell);
            Abort_Frame(f);
            return OUT;
        }
    }

    DROP_GC_GUARD(cell);
    Drop_Frame(f);
    return OUT;
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
DECLARE_NATIVE(case)
{
    const bool choose = false; // jsut a plain CASE
    return Case_Choose_Core_May_Throw(frame_, choose);
}


//
//  choose: native [
//
//  {Evaluates each condition, and gives back the value that follows it}
//
//      return: [<opt> any-value!]
//          "Last matched choice value, or void if no choices matched"
//      choices [block!]
//          "Evaluate all choices (do not stop at first TRUTHY? choice)"
//      /all ;-- see note
//          "Return the value for the last matched choice (instead of first)"
//  ]
//
DECLARE_NATIVE(choose)
//
// Note: The choose can't be run backwards, only forwards.  So implementation
// means that "/LAST" really can only be done as an /ALL, there's no way to
// go backwards in the block and get a Rebol-coherent answer.  Calling it /ALL
// instead of /LAST helps reinforce that *all the conditions* are evaluated.
{
    const bool choose = true; // do a CHOOSE as opposed to a CASE
    return Case_Choose_Core_May_Throw(frame_, choose);
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
DECLARE_NATIVE(switch)
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

    Value* value = ARG(value);

    Init_Nulled(OUT); // used for "fallout"

    while (NOT_END(f->value)) {
        //
        // If a branch is seen at this point, it doesn't correspond to any
        // condition to match.  If no more tests are run, let it suppress the
        // feature of the last value "falling out" the bottom of the switch
        //
        if (IS_BLOCK(f->value)) {
            Init_Nulled(OUT);
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
            Quote_Next_In_Frame(OUT, f);
        else {
            if (Eval_Step_Throws(SET_END(OUT), f)) {
                Abort_Frame(f);
                return R_THROWN;
            }

            if (IS_END(OUT)) {
                assert(IS_END(f->value));
                Init_Nulled(OUT);
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

        if (!Compare_Modify_Values(value, OUT, REF(strict) ? 1 : 0))
            continue;

        // Skip ahead to try and find a block, to treat as code for the match

        while (true) {
            if (IS_END(f->value)) {
                Drop_Frame(f);
                return OUT; // last test "falls out", might be void
            }
            if (IS_BLOCK(f->value))
                break;
            if (IS_ACTION(f->value))
                goto action_not_supported; // literal action
            Fetch_Next_In_Frame(nullptr, f);
        }

        if (Do_At_Throws( // it's a match, so run the BLOCK!
            OUT,
            Cell_Array(f->value),
            VAL_INDEX(f->value),
            f->specifier
        )){
            Abort_Frame(f);
            return R_THROWN;
        }

        Trashify_If_Nulled(OUT);  // null is reserved for no branch run

        if (not REF(all)) {
            Abort_Frame(f);
            return OUT;
        }

        Fetch_Next_In_Frame(nullptr, f); // keep matching if /ALL
    }

    Drop_Frame(f);
    return OUT; // last test "falls out" or last match if /ALL, may be void
}


//
//  default: enfix native [
//
//  {Set word or path to a default value if it is not set yet or blank.}
//
//      return: "Former value or branch result, can only be null if no target"
//          [<opt> any-value!]
//     :target "Word or path which might be set--no target always branches"
//          [<skip> set-word! set-path!]
//      branch "If target not set already, this is evaluated and stored there"
//          [block! action!]
//      :look "Variadic lookahead used to make sure at end if no target"
//          [<...>]
//      /only "Consider target being BLANK! to be a value not to overwrite"
//  ]
//
DECLARE_NATIVE(default)
{
    INCLUDE_PARAMS_OF_DEFAULT;

    Value* target = ARG(target);

    if (IS_NULLED(target)) { // e.g. `case [... default [...]]`
        UNUSED(ARG(look));
        if (NOT_END(frame_->value)) // !!! shortcut using variadic for now
            fail ("DEFAULT usage with no left hand side must be at <end>");

        if (Do_Branch_Throws(OUT, ARG(branch)))
            return R_THROWN;

        return OUT; // NULL is okay in this case
    }

    if (IS_SET_WORD(target))
        Move_Opt_Var_May_Fail(OUT, target, SPECIFIED);
    else {
        assert(IS_SET_PATH(target));
        Get_Path_Core(OUT, target, SPECIFIED); // will fail() on GROUP!s
    }

    if (
        not IS_NULLED(OUT)
        and not IS_TRASH(OUT)
        and (not IS_BLANK(OUT) or REF(only))
    ){
        return OUT;  // count it as "already set"
    }

    if (Do_Branch_Throws(OUT, ARG(branch)))
        return R_THROWN;

    if (IS_NULLED(OUT))
        fail ("DEFAULT came back NULL"); // !!! Review--what about BLANK!

    const bool enfix = false;
    if (IS_SET_WORD(target))
        Move_Value(Sink_Var_May_Fail(target, SPECIFIED), OUT);
    else {
        assert(IS_SET_PATH(target));
        Set_Path_Core(target, SPECIFIED, OUT, enfix);
    }
    return OUT;
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
//      /result "Optional evaluation result if not thrown"
//      uncaught [word! path!]
//      /name "Catches a named throw"  ;-- should it be called /named ?
//      names "Names to catch (single name if not block)"
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
//
// !!! R3C lacks multiple return value handling, but this gives parity with
// a /RESULT refinement for getting the mechanical result.  In mainline you
// could write:
//
//     [caught uncaught]: catch [...]
//
// But R3C will have to do this as:
//
//     catch: catch/result [...] 'uncaught
//
// When the value being set is TRASH, SET/ANY must be used at this time.  This
// is a bit more inefficient in the API since it requires scanning.  Non-void
// cases are done with directly referencing the SET native.
{
    INCLUDE_PARAMS_OF_CATCH;

    // /ANY would override /NAME, so point out the potential confusion
    //
    if (REF(any) and REF(name))
        fail (Error_Bad_Refines_Raw());

    if (not Do_Any_Array_At_Throws(OUT, ARG(block))) {
        if (REF(result))
            rebElide(rebEval(NAT_VALUE(set)), ARG(uncaught), OUT);

        return nullptr;  // no throw means just return null
    }

    if (REF(any) and not (
        IS_ACTION(OUT)
        and VAL_ACT_DISPATCHER(OUT) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(quit) and (
        IS_ACTION(OUT)
        and VAL_ACT_DISPATCHER(OUT) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(name)) {
        //
        // We use equal? by way of Compare_Modify_Values, and re-use the
        // refinement slots for the mutable space

        Value* temp1 = ARG(quit);
        Value* temp2 = ARG(any);

        // !!! The reason we're copying isn't so the VALUE_FLAG_THROWN bit
        // won't confuse the equality comparison...but would it have?

        if (IS_BLOCK(ARG(names))) {
            //
            // Test all the words in the block for a match to catch

            Cell* candidate = Cell_Array_At(ARG(names));
            for (; NOT_END(candidate); candidate++) {
                //
                // !!! Should we test a typeset for illegal name types?
                //
                if (IS_BLOCK(candidate))
                    fail (Error_Invalid(ARG(names)));

                Derelativize(temp1, candidate, VAL_SPECIFIER(ARG(names)));
                Move_Value(temp2, OUT);

                // Return the THROW/NAME's arg if the names match
                // !!! 0 means equal?, but strict-equal? might be better
                //
                if (Compare_Modify_Values(temp1, temp2, 0))
                    goto was_caught;
            }
        }
        else {
            Move_Value(temp1, ARG(names));
            Move_Value(temp2, OUT);

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
        if (IS_BLANK(OUT) and (REF(any) or not REF(quit)))
            goto was_caught;
    }

    return R_THROWN; // throw name is in OUT, value is held task local

  was_caught:

    if (REF(name) or REF(any)) {
        Array* a = Make_Arr(2);

        CATCH_THROWN(Array_At(a, 1), OUT); // thrown value--may be null!
        Move_Value(Array_At(a, 0), OUT); // throw name (thrown bit clear)
        if (IS_NULLED(Array_At(a, 1)))
            TERM_ARRAY_LEN(a, 1); // trim out null value (illegal in block)
        else
            TERM_ARRAY_LEN(a, 2);
        return Init_Block(OUT, a);
    }
    else
        CATCH_THROWN(OUT, OUT);  // thrown value

    // !!! You are not allowed to run evaluations while a throw is in effect,
    // so this assignment has to wait until the end.
    //
    if (REF(result))  // caught case voids result to minimize likely use
        rebElide(NAT_VALUE(set), ARG(uncaught), TRASH_VALUE);

    return OUT;
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
DECLARE_NATIVE(throw)
//
// Choices are currently limited for what one can use as a "name" of a THROW.
// Note blocks as names would conflict with the `name_list` feature in CATCH.
//
// !!! Should parameters be /NAMED and NAME ?
{
    INCLUDE_PARAMS_OF_THROW;

    Value* value = ARG(value);

    if (REF(name))
        Move_Value(OUT, ARG(name_value));
    else {
        // Blank values serve as representative of THROWN() means "no name"
        //
        Init_Blank(OUT);
    }

    CONVERT_NAME_TO_THROWN(OUT, value);
    return R_THROWN;
}
