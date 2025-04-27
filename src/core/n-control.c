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
//   return a NOTHING result.  Since NOTHING are values (unlike NULL), they
//   distinguish the signal of a branch that evaluated to NULL from no branch
//   at all.
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
//          [any-value!]
//      condition [any-value!]
//      branch "If arity-1 ACTION!, receives the evaluated condition"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(IF)
{
    INCLUDE_PARAMS_OF_IF;

    if (IS_FALSEY(ARG(CONDITION))) // fails on void and trash
        return Init_Void(OUT);

    if (Do_Branch_With_Throws(OUT, ARG(BRANCH), ARG(CONDITION)))
        return BOUNCE_THROWN;

    return Trashify_Branched(OUT);  // trash means no branch (cues ELSE)
}


//
//  if-not: native [
//
//  {When TO LOGIC! CONDITION is false, execute branch}
//
//      return: "null if branch not run, otherwise branch result"
//          [any-value!]
//      condition [any-value!]
//      branch [block! action!]
//  ]
//
DECLARE_NATIVE(IF_NOT)
{
    INCLUDE_PARAMS_OF_IF_NOT;

    if (IS_TRUTHY(ARG(CONDITION)))  // fails on void and trash
        return nullptr;

    if (Do_Branch_With_Throws(OUT, ARG(BRANCH), ARG(CONDITION)))
        return BOUNCE_THROWN;

    return Trashify_Branched(OUT);  // trash means no branch (cues ELSE)
}


//
//  either: native [
//
//  {Choose a branch to execute, based on TO-LOGIC of the CONDITION value}
//
//      return: [any-value!]
//          "Returns null if either branch returns null (unlike IF...ELSE)"
//      condition [any-value!]
//      true-branch "If arity-1 ACTION!, receives the evaluated condition"
//          [block! action!]
//      false-branch [block! action!]
//  ]
//
DECLARE_NATIVE(EITHER)
{
    INCLUDE_PARAMS_OF_EITHER;

    if (Do_Branch_With_Throws(
        OUT,
        IS_TRUTHY(ARG(CONDITION))  // fails on void and trash
            ? ARG(TRUE_BRANCH)
            : ARG(FALSE_BRANCH),
        ARG(CONDITION)
    )){
        return BOUNCE_THROWN;
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
    switch (Type_Of(test)) {

      case TYPE_WORD:
      case TYPE_PATH: {
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
        StackIndex lowest_stackindex = TOP_INDEX;
        if (Get_If_Word_Or_Path_Throws(
            out,
            &opt_label,
            test,
            SPECIFIED,
            push_refinements
        )){
            return true;
        }

        assert(lowest_stackindex == TOP_INDEX);  // would've specialized
        UNUSED(lowest_stackindex);

        Copy_Cell(test, out);

        if (not Is_Action(test))
            fail ("EITHER-TEST only takes WORD! and PATH! for ACTION! vars");
        goto handle_action; }

      case TYPE_ACTION: {
      handle_action:;

        if (Apply_Only_Throws(
            out,
            true, // `fully` (ensure argument consumed)
            test,
            arg,  // may be nulled cell
            rebEND
        )){
            return true;
        }

        Init_Logic(out, IS_TRUTHY(out));
        return false; }

      case TYPE_DATATYPE:
        Init_Logic(out, CELL_DATATYPE_TYPE(test) == Type_Of(arg));
        return false;

      case TYPE_TYPESET:
        Init_Logic(out, Typeset_Check(test, Type_Of(arg)));
        return false;

    case TYPE_BLOCK: {
        Cell* item = Cell_List_At(test);
        if (IS_END(item)) {
            //
            // !!! If the test is just [], what's that?  People aren't likely
            // to write it literally, but COMPOSE/etc. might make it.
            //
            fail ("No tests found BLOCK! passed to EITHER-TEST.");
        }

        Specifier* specifier = VAL_SPECIFIER(test);
        for (; NOT_END(item); ++item) {
            const Cell* var;
            if (not Is_Word(item))
                var = item;
            else {
                if (Cell_Word_Id(item) == SYM__TNULL_T) {
                    if (Is_Nulled(arg)) {
                        Init_Logic(out, true);
                        return false;
                    }
                    continue;
                }
                if (Cell_Word_Id(item) == SYM__TOKAY_T) {
                    if (Is_Okay(arg)) {
                        Init_Logic(out, true);
                        return false;
                    }
                    continue;
                }
                if (Cell_Word_Id(item) == SYM__TVOID_T) {
                    if (Is_Void(arg)) {
                        Init_Logic(out, true);
                        return false;
                    }
                    continue;
                }
                var = Get_Opt_Var_May_Fail(item, specifier);
            }

            if (Is_Datatype(var)) {
                if (CELL_DATATYPE_TYPE(var) == Type_Of(arg)) {
                    Init_Logic(out, true);
                    return false;
                }
            }
            else if (Is_Typeset(var)) {
                if (Typeset_Check(var, Type_Of(arg))) {
                    Init_Logic(out, true);
                    return false;
                }
            }
            else
                fail (Error_Invalid_Type(Type_Of(var)));
        }
        Init_Logic(out, false);
        return false; }

      default:
        break;
    }

    fail (Error_Invalid_Type(Type_Of(arg)));
}


//
//  either-test: native [
//
//  {If argument passes test, return it as-is, otherwise take the branch}
//
//      return: "Input argument if it matched, or branch result"
//          [any-value!]
//      test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              word! path! action! ;-- arity-1 filter function, opt named
//              datatype! typeset! block! ;-- typeset specification forms
//              logic! ;-- tests TO-LOGIC compatibility
//          ]
//      arg [any-value!]
//      branch "If arity-1 ACTION!, receives the non-matching argument"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(EITHER_TEST)
{
    INCLUDE_PARAMS_OF_EITHER_TEST;

    if (Either_Test_Core_Throws(OUT, ARG(TEST), ARG(ARG)))
        return BOUNCE_THROWN;

    if (VAL_LOGIC(OUT))
        RETURN (ARG(ARG));

    if (Do_Branch_With_Throws(OUT, ARG(BRANCH), ARG(ARG)))
        return BOUNCE_THROWN;

    return OUT;
}


//
//  else: infix/defer native [
//
//  {If input is not null, return that value, otherwise evaluate the branch}
//
//      return: "Input value if not null, or branch result (possibly null)"
//          [any-value!]
//      left "Run branch if this is null or void"
//          [any-value! trash!]
//      branch [block! action!]
//  ]
//
DECLARE_NATIVE(ELSE)
{
    INCLUDE_PARAMS_OF_ELSE; // faster than EITHER-TEST specialized w/`VALUE?`

    Value* left = ARG(LEFT);
    if (not Is_Nulled(left) and not Is_Void(left))
        RETURN (left);

    if (Do_Branch_With_Throws(OUT, ARG(BRANCH), NULLED_CELL))
        return BOUNCE_THROWN;

    return OUT;  // don't trashify, allows chaining: `else [...] then [...]`
}


//
//  then: infix/defer native [
//
//  {If input is null, return null, otherwise evaluate the branch}
//
//      return: "null if input is null, or branch result (voided if null)"
//          [any-value!]
//      left "Run branch if this is not null or void"
//          [any-value! trash!]
//      branch "If arity-1 ACTION!, receives value that triggered branch"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(THEN)
{
    INCLUDE_PARAMS_OF_THEN; // faster than EITHER-TEST specialized w/`NULL?`

    Value* left = ARG(LEFT);
    if (Is_Nulled(left) or Is_Void(left))
        return nullptr;  // left didn't run, so signal THEN didn't run either

    if (Do_Branch_With_Throws(OUT, ARG(BRANCH), left))
        return BOUNCE_THROWN;

    return Trashify_Branched(OUT);  // if left ran, make THEN signal it did
}


//
//  also: infix/defer native [
//
//  {For non-null input, evaluate and discard branch (like a pass-thru THEN)}
//
//      return: "The same value as input, regardless of if branch runs"
//          [any-value!]
//      left "Run branch if this is not null or void"
//          [any-value! trash!]
//      branch "If arity-1 ACTION!, receives value that triggered branch"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(ALSO)
{
    INCLUDE_PARAMS_OF_ALSO; // `then func [x] [(...) :x]` => `also [...]`

    Value* left = ARG(LEFT);
    if (Is_Nulled(left) or Is_Void(left))
        return nullptr;

    if (Do_Branch_With_Throws(OUT, ARG(BRANCH), left))
        return BOUNCE_THROWN;

    RETURN (left); // just passing thru the input
}


//
//  match: native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input if it matched, otherwise null (void if falsey match)"
//          [any-value!]
//      test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              datatype! typeset! block! logic! action! ;-- like EITHER-TEST
//          ]
//      value [any-value!]
//  ]
//
DECLARE_NATIVE(MATCH)
{
    INCLUDE_PARAMS_OF_MATCH;

    Value* test = ARG(TEST);

    Value* value = ARG(VALUE);

    DECLARE_VALUE (temp);
    if (Either_Test_Core_Throws(temp, test, value))
        return BOUNCE_THROWN;

    if (VAL_LOGIC(temp)) {
        if (IS_FALSEY(value)) // see above for why false match not passed thru
            return Init_Trash(OUT);
        return Copy_Cell(OUT, value);
    }

    return nullptr;
}


//
//  non: native [
//
//  {Make sure a value does NOT match a type constraint (see also: ENSURE)}
//
//      return: "Input value if it passes the type test"
//          [any-value!]
//      test "The test to apply (limited to DATATYPE! and NULL at this time)"
//          [~null~ datatype!]
//      value "Value to test (will either be returned as result or error)"
//          [any-value!]
// ]
//
DECLARE_NATIVE(NON)
//
// !!! This is a partial implementation of NON implemented for R3C, just good
// enough for `non [trash!]` and `non null` cases to give validation options to
// those wanting a less permissive SET (now that it has no /ANY refinement).
{
    INCLUDE_PARAMS_OF_NON;

    Value* test = ARG(TEST);
    Value* value = ARG(VALUE);

    if (Is_Nulled(test)) {  // not a datatype, needs special case
        if (Is_Nulled(value))
            fail ("NON expected value to not be NULL, but it was");
    }
    else if (CELL_DATATYPE_TYPE(test) == TYPE_TRASH) {  // specialize common case
        if (Is_Trash(value))
            fail ("NON expected value to not be trash, but it was");
    }
    else if (not Typeset_Check(value, CELL_DATATYPE_TYPE(test))) {
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
//          [any-value!]
//      block "Block of expressions"
//          [block!]
//  ]
//
DECLARE_NATIVE(ALL)
{
    INCLUDE_PARAMS_OF_ALL;

    DECLARE_LEVEL (L);
    Push_Level(L, ARG(BLOCK));

    Init_Void(OUT);  // default return result

    while (NOT_END(L->value)) {
        if (Eval_Step_Throws(SET_END(SPARE), L)) {
            Copy_Cell(OUT, SPARE);
            Abort_Level(L);
            return BOUNCE_THROWN;
        }

        if (Is_Void(SPARE))
            continue;

        if (IS_FALSEY(SPARE)) {  // nulled will trigger failure
            Abort_Level(L);
            return nullptr;
        }

        Copy_Cell(OUT, SPARE); // successful ALL returns the last truthy value
    }

    Drop_Level(L);
    return OUT; // successful ALL when the last OUT assignment is truthy
}


//
//  any: native [
//
//  {Short-circuiting version of OR, using a block of expressions as input}
//
//      return: "First truthy evaluative result, or null if all falsey"
//          [any-value!]
//      block "Block of expressions"
//          [block!]
//  ]
//
DECLARE_NATIVE(ANY)
{
    INCLUDE_PARAMS_OF_ANY;

    DECLARE_LEVEL (L);
    Push_Level(L, ARG(BLOCK));

    Init_Void(OUT);  // default return result

    while (NOT_END(L->value)) {
        if (Eval_Step_Throws(SET_END(SPARE), L)) {
            Copy_Cell(OUT, SPARE);
            Abort_Level(L);
            return BOUNCE_THROWN;
        }

        if (Is_Void(SPARE))
            continue;

        if (IS_TRUTHY(SPARE)) { // successful ANY returns the value
            Abort_Level(L);
            return Copy_Cell(OUT, SPARE);
        }
    }

    Drop_Level(L);
    return nullptr;
}


//
//  none: native [
//
//  {Short circuiting version of NOR, using a block of expressions as input}
//
//      return: "okay if expressions are null, null if any are not, skip void"
//          [logic!]
//      block "Block of expressions."
//          [block!]
//  ]
//
DECLARE_NATIVE(NONE)
//
// !!! In order to reduce confusion and accidents in the near term, the
// %mezz-legacy.r renames this to NONE-OF and makes NONE report an error.
{
    INCLUDE_PARAMS_OF_NONE;

    DECLARE_LEVEL (L);
    Push_Level(L, ARG(BLOCK));

    while (NOT_END(L->value)) {
        if (Eval_Step_Throws(SET_END(SPARE), L)) {
            Abort_Level(L);
            return BOUNCE_THROWN;
        }

        if (Is_Void(SPARE))
            continue;

        if (IS_TRUTHY(SPARE)) { // any true results mean failure
            Abort_Level(L);
            return nullptr;
        }
    }

    Drop_Level(L);
    return Init_Okay(OUT);
}


// Shared code for CASE (which runs BLOCK! clauses as code) and CHOOSE (which
// returns values as-is, e.g. `choose [okay [print "hi"]]` => `[print "hi]`
//
static Bounce Case_Choose_Core_May_Throw(
    Level* level_,
    bool choose // do not evaluate branches, just "choose" them
){
    INCLUDE_PARAMS_OF_CASE;

    Value* block = ARG(CASES); // for CHOOSE, it's "choices" not "cases"

    DECLARE_LEVEL (L);
    Push_Level(L, block); // array GC safe now, can re-use `block` cell

    Init_Nulled(OUT); // default return result

    DECLARE_VALUE (cell); // unsafe to use ARG() slots as frame's L->out
    SET_END(cell);
    Push_GC_Guard(cell);

    while (NOT_END(L->value)) {

        // Perform 1 EVALUATE's worth of evaluation on a "condition" to test
        // Will consume any pending "invisibles" (COMMENT, ELIDE, DUMP...)

        if (Eval_Step_Throws(SET_END(cell), L)) {
            Drop_GC_Guard(cell);
            Copy_Cell(OUT, cell);
            Abort_Level(L);
            return BOUNCE_THROWN;
        }

        if (IS_END(cell)) {  // !!! R3C patch for permissive invisibility
            if (IS_END(L->value))
                break;
            continue;
        }

        // The last condition will "fall out" if there is no branch/choice:
        //
        //     case [1 > 2 [...] 3 > 4 [...] 10 + 20] = 30
        //     choose [1 > 2 (literal group) 3 > 4 <tag> 10 + 20] = 30
        //
        if (IS_END(L->value)) {
            Drop_GC_Guard(cell);
            Drop_Level(L);
            return Copy_Cell(OUT, cell);
        }

        if (IS_FALSEY(cell)) {  // not a matching condition
            if (choose) {
                Fetch_Next_In_Level(nullptr, L); // skip next, whatever it is
                continue;
            }

            // Even if branch is being skipped, it gets an evaluation--like
            // how `if null (print "A" [print "B"])` prints A, but not B.
            //
            if (Eval_Step_Throws(SET_END(cell), L)) {
                Abort_Level(L);
                // preserving `out` value (may be previous match)
                Drop_GC_Guard(cell);
                return Copy_Cell(OUT, cell);
            }

            if (IS_END(cell))
                continue;  // a COMMENT

            // Maintain symmetry with IF's typechecking of non-taken branches:
            //
            // >> if null <some-tag>
            // ** Script Error: if does not allow tag! for its branch argument
            //
            if (not Is_Block(cell) and not Is_Action(cell))
                fail (Error_Invalid_Core(cell, L->specifier));

            continue;
        }

        if (choose) {
            Derelativize(OUT, L->value, L->specifier); // null not possible
            Fetch_Next_In_Level(nullptr, L); // keep matching if /ALL
        }
        else {
            // Note: we are preserving `cell` to pass to an arity-1 ACTION!

            if (Eval_Step_Throws(SET_END(OUT), L)) {
                Drop_GC_Guard(cell);
                Abort_Level(L);
                return BOUNCE_THROWN;
            }

            L->gotten = nullptr; // can't hold onto cache, running user code

            Copy_Cell(block, OUT); // can't evaluate into ARG(BLOCK)
            if (Is_Block(block)) {
                if (Eval_List_At_Throws(OUT, block)) {
                    Abort_Level(L);
                    Drop_GC_Guard(cell);
                    return BOUNCE_THROWN;
                }
            }
            else if (Is_Action(OUT)) {
                if (Do_Branch_With_Throws(OUT, block, cell)) {
                    Abort_Level(L);
                    Drop_GC_Guard(cell);
                    return BOUNCE_THROWN;
                }
            } else
                fail (Error_Invalid_Core(OUT, L->specifier));

            Trashify_Branched(OUT);  // null is reserved for no branch taken
        }

        if (not Bool_ARG(ALL)) {
            Drop_GC_Guard(cell);
            Abort_Level(L);
            return OUT;
        }
    }

    Drop_GC_Guard(cell);
    Drop_Level(L);
    return OUT;
}


//
//  case: native [
//
//  {Evaluates each condition, and when true, evaluates what follows it}
//
//      return: [any-value!]
//          "Last matched case evaluation, or null if no cases matched"
//      cases [block!]
//          "Block of cases (conditions followed by branches)"
//      /all
//          "Evaluate all cases (do not stop at first logically true case)"
//  ]
//
DECLARE_NATIVE(CASE)
{
    const bool choose = false; // jsut a plain CASE
    return Case_Choose_Core_May_Throw(level_, choose);
}


//
//  choose: native [
//
//  {Evaluates each condition, and gives back the value that follows it}
//
//      return: [any-value!]
//          "Last matched choice value, or void if no choices matched"
//      choices [block!]
//          "Evaluate all choices (do not stop at first TRUTHY? choice)"
//      /all ;-- see note
//          "Return the value for the last matched choice (instead of first)"
//  ]
//
DECLARE_NATIVE(CHOOSE)
//
// Note: The choose can't be run backwards, only forwards.  So implementation
// means that "/LAST" really can only be done as an /ALL, there's no way to
// go backwards in the block and get a Rebol-coherent answer.  Calling it /ALL
// instead of /LAST helps reinforce that *all the conditions* are evaluated.
{
    const bool choose = true; // do a CHOOSE as opposed to a CASE
    return Case_Choose_Core_May_Throw(level_, choose);
}


//
//  switch: native [
//
//  {Selects a choice and evaluates the block that follows it.}
//
//      return: "Last case evaluation, or null if no cases matched"
//          [any-value!]
//      value "Target value"
//          [any-value!]
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
DECLARE_NATIVE(SWITCH)
{
    INCLUDE_PARAMS_OF_SWITCH;

    if (Bool_ARG(DEFAULT))
        fail (
            "SWITCH/DEFAULT is no longer supported by the core.  Use the"
            " DEFAULT [...] as the last clause, or ELSE/UNLESS/!!/etc. based"
            " on null result: https://forum.rebol.info/t/312"
        );
    UNUSED(ARG(DEFAULT_BRANCH));

    DECLARE_LEVEL (L);
    Push_Level(L, ARG(CASES));

    Value* value = ARG(VALUE);

    Init_Nulled(OUT); // used for "fallout"

    while (NOT_END(L->value)) {
        //
        // If a branch is seen at this point, it doesn't correspond to any
        // condition to match.  If no more tests are run, let it suppress the
        // feature of the last value "falling out" the bottom of the switch
        //
        if (Is_Block(L->value)) {
            Init_Nulled(OUT);
            Fetch_Next_In_Level(nullptr, L);
            continue;
        }

        if (Is_Action(L->value)) {
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

        if (Bool_ARG(QUOTE))
            Quote_Next_In_Level(OUT, L);
        else {
            if (Eval_Step_Throws(SET_END(OUT), L)) {
                Abort_Level(L);
                return BOUNCE_THROWN;
            }

            if (IS_END(OUT)) {
                assert(IS_END(L->value));
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

        if (!Compare_Modify_Values(value, OUT, Bool_ARG(STRICT) ? 1 : 0))
            continue;

        // Skip ahead to try and find a block, to treat as code for the match

        while (true) {
            if (IS_END(L->value)) {
                Drop_Level(L);
                return OUT; // last test "falls out", might be void
            }
            if (Is_Block(L->value))
                break;
            if (Is_Action(L->value))
                goto action_not_supported; // literal action
            Fetch_Next_In_Level(nullptr, L);
        }

        if (Eval_Array_At_Throws( // it's a match, so run the BLOCK!
            OUT,
            Cell_Array(L->value),
            VAL_INDEX(L->value),
            L->specifier
        )){
            Abort_Level(L);
            return BOUNCE_THROWN;
        }

        Trashify_Branched(OUT);  // null is reserved for no branch run

        if (not Bool_ARG(ALL)) {
            Abort_Level(L);
            return OUT;
        }

        Fetch_Next_In_Level(nullptr, L); // keep matching if /ALL
    }

    Drop_Level(L);
    return OUT; // last test "falls out" or last match if /ALL, may be void
}


//
//  default: infix native [
//
//  {Set word or path to a default value if it is not set yet or blank.}
//
//      return: "Former value or branch result, can only be null if no target"
//          [any-value!]
//     :target "Word or path which might be set--no target always branches"
//          [<skip> set-word! set-path!]
//      branch "If target not set already, this is evaluated and stored there"
//          [block! action!]
//      :look "Variadic lookahead used to make sure at end if no target"
//          [<...>]
//      /only "Consider target being BLANK! to be a value not to overwrite"
//  ]
//
DECLARE_NATIVE(DEFAULT)
{
    INCLUDE_PARAMS_OF_DEFAULT;

    Value* target = ARG(TARGET);

    if (Is_Nulled(target)) { // e.g. `case [... default [...]]`
        UNUSED(ARG(LOOK));
        if (NOT_END(level_->value)) // !!! shortcut using variadic for now
            fail ("DEFAULT usage with no left hand side must be at <end>");

        if (Do_Branch_Throws(OUT, ARG(BRANCH)))
            return BOUNCE_THROWN;

        return OUT; // NULL is okay in this case
    }

    if (Is_Set_Word(target))
        Move_Opt_Var_May_Fail(OUT, target, SPECIFIED);
    else {
        assert(Is_Set_Path(target));
        Get_Path_Core(OUT, target, SPECIFIED); // will fail() on GROUP!s
    }

    if (
        not Is_Nulled(OUT)
        and not Is_Trash(OUT)
        and (not Is_Blank(OUT) or Bool_ARG(ONLY))
    ){
        return OUT;  // count it as "already set"
    }

    if (Do_Branch_Throws(OUT, ARG(BRANCH)))
        return BOUNCE_THROWN;

    if (Is_Set_Word(target))
        Copy_Cell(Sink_Var_May_Fail(target, SPECIFIED), OUT);
    else {
        assert(Is_Set_Path(target));
        Set_Path_Core(target, SPECIFIED, OUT);
    }
    return OUT;
}


//
//  catch: native [
//
//  {Catches a throw from a block and returns its value.}
//
//      return: "Thrown value, or BLOCK! with value and name (if /NAME, /ANY)"
//          [any-value!]
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
DECLARE_NATIVE(CATCH)
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
// When the value being set is NOTHING, SET/ANY must be used for now.  This
// is a bit more inefficient in the API since it requires scanning.  Non-void
// cases are done with directly referencing the SET native.
{
    INCLUDE_PARAMS_OF_CATCH;

    // /ANY would override /NAME, so point out the potential confusion
    //
    if (Bool_ARG(ANY) and Bool_ARG(NAME))
        fail (Error_Bad_Refines_Raw());

    if (not Eval_List_At_Throws(OUT, ARG(BLOCK))) {
        if (Bool_ARG(RESULT))
            rebElide(NAT_VALUE(SET), ARG(UNCAUGHT), OUT);

        return nullptr;  // no throw means just return null
    }

    if (Bool_ARG(ANY) and not (
        Is_Action(OUT)
        and VAL_ACT_DISPATCHER(OUT) == &NATIVE_CFUNC(QUIT)
    )){
        goto was_caught;
    }

    if (Bool_ARG(QUIT) and (
        Is_Action(OUT)
        and VAL_ACT_DISPATCHER(OUT) == &NATIVE_CFUNC(QUIT)
    )){
        goto was_caught;
    }

    if (Bool_ARG(NAME)) {
        //
        // We use equal? by way of Compare_Modify_Values, and re-use the
        // refinement slots for the mutable space

        Value* temp1 = ARG(QUIT);
        Value* temp2 = ARG(ANY);

        // !!! The reason we're copying isn't so the CELL_FLAG_THROW_SIGNAL
        // bit won't confuse the equality comparison...but would it have?

        if (Is_Block(ARG(NAMES))) {
            //
            // Test all the words in the block for a match to catch

            Cell* candidate = Cell_List_At(ARG(NAMES));
            for (; NOT_END(candidate); candidate++) {
                //
                // !!! Should we test a typeset for illegal name types?
                //
                if (Is_Block(candidate))
                    fail (Error_Invalid(ARG(NAMES)));

                Derelativize(temp1, candidate, VAL_SPECIFIER(ARG(NAMES)));
                Copy_Cell(temp2, OUT);

                // Return the THROW/NAME's arg if the names match
                // !!! 0 means equal?, but strict-equal? might be better
                //
                if (Compare_Modify_Values(temp1, temp2, 0))
                    goto was_caught;
            }
        }
        else {
            Copy_Cell(temp1, ARG(NAMES));
            Copy_Cell(temp2, OUT);

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
        if (Is_Blank(OUT) and (Bool_ARG(ANY) or not Bool_ARG(QUIT)))
            goto was_caught;
    }

    return BOUNCE_THROWN; // throw name is in OUT, value is held task local

  was_caught:

    if (Bool_ARG(NAME) or Bool_ARG(ANY)) {
        Array* a = Make_Array(2);

        CATCH_THROWN(Array_At(a, 1), OUT); // thrown value--may be null!
        Copy_Cell(Array_At(a, 0), OUT); // throw name (thrown bit clear)
        if (Is_Nulled(Array_At(a, 1)))
            Term_Array_Len(a, 1); // trim out null value (illegal in block)
        else
            Term_Array_Len(a, 2);
        return Init_Block(OUT, a);
    }
    else
        CATCH_THROWN(OUT, OUT);  // thrown value

    // !!! You are not allowed to run evaluations while a throw is in effect,
    // so this assignment has to wait until the end.
    //
    if (Bool_ARG(RESULT))  // caught case voids result to minimize likely use
        rebElide(NAT_VALUE(SET), ARG(UNCAUGHT), TRASH_VALUE);

    return OUT;
}


//
//  throw: native [
//
//  "Throws control back to a previous catch."
//
//      value "Value returned from catch"
//          [any-value!]
//      /name "Throws to a named catch"
//      name-value [word! action! object!]
//  ]
//
DECLARE_NATIVE(THROW)
//
// Choices are currently limited for what one can use as a "name" of a THROW.
// Note blocks as names would conflict with the `name_list` feature in CATCH.
//
// !!! Should parameters be /NAMED and NAME ?
{
    INCLUDE_PARAMS_OF_THROW;

    Value* value = ARG(VALUE);

    if (Bool_ARG(NAME))
        Copy_Cell(OUT, ARG(NAME_VALUE));
    else {
        // Blank values serve as representative of THROWN() means "no name"
        //
        Init_Blank(OUT);
    }

    CONVERT_NAME_TO_THROWN(OUT, value);
    return BOUNCE_THROWN;
}
