//
//  File: %t-varargs.h
//  Summary: "Variadic Argument Type and Services"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The VARARGS! data type implements an abstraction layer over an eval levle
// or arbitrary array of values.  All copied instances of a REB_VARARGS value
// remain in sync as values are TAKE-d out of them.  Once they report
// reaching a TAIL? they will always report TAIL?...until the call that
// spawned them is off the stack, at which point they will report an error.
//

#include "sys-core.h"


INLINE void Init_For_Vararg_End(Atom* out, enum Reb_Vararg_Op op) {
    if (op == VARARG_OP_TAIL_Q)
        Init_True(out);
    else
        Init_Barrier(out);
}


// Some VARARGS! are generated from a block with no level, while others
// have a level.  It would be inefficient to force the creation of a level on
// each call for a BLOCK!-based varargs.  So rather than doing so, there's a
// prelude which sees if it can answer the current query just from looking one
// unit ahead.
//
INLINE bool Vararg_Op_If_No_Advance_Handled(
    Atom* out,
    enum Reb_Vararg_Op op,
    Option(const Element*) opt_look, // the first value in the varargs input
    Specifier* specifier,
    ParamClass pclass
){
    if (not opt_look) {
        Init_For_Vararg_End(out, op); // exhausted
        return true;
    }

    const Element* look = unwrap(opt_look);

    if (pclass == PARAMCLASS_NORMAL and Is_Comma(look)) {
        Init_For_Vararg_End(out, op);  // non-quoted COMMA!
        return true;
    }

    if (pclass == PARAMCLASS_NORMAL and Is_Word(look)) {
        //
        // When a variadic argument is being TAKE-n, deferred left hand side
        // argument needs to be seen as end of variadic input.  Otherwise,
        // `summation 1 2 3 |> 100` acts as `summation 1 2 (3 |> 100)`.
        // Deferred operators need to act somewhat as an expression barrier.
        //
        // Same rule applies for "tight" arguments, `sum 1 2 3 + 4` with
        // sum being variadic and tight needs to act as `(sum 1 2 3) + 4`
        //
        // Look ahead, and if actively bound see if it's to an enfix function
        // and the rules apply.

        const Value* child_gotten = try_unwrap(Lookup_Word(look, specifier));

        if (child_gotten and Is_Action(child_gotten)) {
            if (Is_Enfixed(child_gotten)) {
                if (
                    pclass == PARAMCLASS_NORMAL or
                    Get_Action_Flag(VAL_ACTION(child_gotten), DEFERS_LOOKBACK)
                ){
                    Init_For_Vararg_End(out, op);
                    return true;
                }
            }
        }
    }

    // The odd circumstances which make things simulate END--as well as an
    // actual END--are all taken care of, so we're not "at the TAIL?"
    //
    if (op == VARARG_OP_TAIL_Q) {
        Init_False(out);
        return true;
    }

    if (op == VARARG_OP_FIRST) {
        if (pclass != PARAMCLASS_HARD)
            fail (Error_Varargs_No_Look_Raw()); // hard quote only

        Derelativize(out, look, specifier);

        return true; // only a lookahead, no need to advance
    }

    return false; // must advance, may need to create a level to do so
}


//
//  Do_Vararg_Op_Maybe_End_Throws_Core: C
//
// Service routine for working with a VARARGS!.  Supports TAKE-ing or just
// returning whether it's at the end or not.  The TAKE is not actually a
// destructive operation on underlying data--merely a semantic chosen to
// convey feeding forward with no way to go back.
//
// Whether the parameter is quoted or evaluated is determined by the typeset
// information of the `param`.  The typeset in the param is also used to
// check the result, and if an error is delivered it will use the name of
// the parameter symbol in the fail() message.
//
// If op is VARARG_OP_TAIL_Q, then it will return Lib(TRUE) or Lib(FALSE),
// and this case cannot return a thrown value.
//
// For other ops, it will return END_NODE if at the end of variadic input,
// or OUT if there is a value.
//
// If an evaluation is involved, then a thrown value is possibly returned.
//
bool Do_Vararg_Op_Maybe_End_Throws_Core(
    Atom* out,
    enum Reb_Vararg_Op op,
    const Cell* vararg,
    ParamClass pclass  // PARAMCLASS_0 to use vararg's class
){
    FRESHEN(out);

    const Key* key;
    const Param* param = Param_For_Varargs_Maybe_Null(&key, vararg);
    if (pclass == PARAMCLASS_0)
        pclass = Cell_ParamClass(param);

    Option(Level*) vararg_level;

    Level* L;
    Element* shared;
    if (Is_Block_Style_Varargs(&shared, vararg)) {
        //
        // We are processing an ANY-ARRAY?-based varargs, which came from
        // either a MAKE VARARGS! on an ANY-ARRAY? value -or- from a
        // MAKE ANY-ARRAY? on a varargs (which reified the varargs into an
        // array during that creation, flattening its entire output).

        vararg_level = nullptr;

        if (Vararg_Op_If_No_Advance_Handled(
            out,
            op,
            Is_Cell_Poisoned(shared) ? nullptr : Cell_Array_Item_At(shared),
            Is_Cell_Poisoned(shared) ? SPECIFIED : Cell_Specifier(shared),
            pclass
        )){
            goto type_check_and_return;
        }

        // Note this may be Is_Varargs_Enfix(), where the left hand side was
        // synthesized into an array-style varargs with either 0 or 1 item to
        // be taken.
        //
        // !!! Note also that if the argument is evaluative, it will be
        // evaluated when the TAKE occurs...which may be never, if no TAKE of
        // this argument happens.  Review if that should be an error.

        switch (pclass) {
        case PARAMCLASS_META:
            fail ("Variadic literal parameters not yet implemented");

        case PARAMCLASS_NORMAL: {
            Level* L_temp = Make_Level_At(
                shared,
                EVAL_EXECUTOR_FLAG_FULFILLING_ARG
            );
            Push_Level(out, L_temp);

            // Note: Eval_Step_In_Sublevel() is not needed here because
            // this is a single use level, whose state can be overwritten.
            //
            if (Eval_Step_Throws(out, L_temp)) {
                Drop_Level(L_temp);
                return true;
            }

            if (Is_Feed_At_End(L_temp->feed) or Is_Barrier(out))
                Poison_Cell(shared);
            else {
                // The indexor is "prefetched", so though the temp level would
                // be ready to use again we're throwing it away, and need to
                // effectively "undo the prefetch" by taking it down by 1.
                //
                assert(Level_Array_Index(L_temp) > 0);
                VAL_INDEX_UNBOUNDED(shared) = Level_Array_Index(L_temp) - 1;
            }

            Drop_Level(L_temp);
            break; }

        case PARAMCLASS_HARD:
            Derelativize(
                out,
                Cell_Array_Item_At(shared),
                Cell_Specifier(shared)
            );
            VAL_INDEX_UNBOUNDED(shared) += 1;
            break;

        case PARAMCLASS_MEDIUM:
            fail ("Variadic medium parameters not yet implemented");

        case PARAMCLASS_SOFT:
            if (ANY_ESCAPABLE_GET(Cell_Array_Item_At(shared))) {
                if (Eval_Value_Throws(
                    out, Cell_Array_Item_At(shared), Cell_Specifier(shared)
                )){
                    return true;
                }
            }
            else { // not a soft-"exception" case, quote ordinarily
                Derelativize(
                    out,
                    Cell_Array_Item_At(shared),
                    Cell_Specifier(shared)
                );
            }
            VAL_INDEX_UNBOUNDED(shared) += 1;
            break;

        default:
            fail ("Invalid variadic parameter class");
        }

        if (
            not Is_Cell_Poisoned(shared)
            and VAL_INDEX(shared) >= Cell_Series_Len_Head(shared)
        ){
            Poison_Cell(shared);  // signal end to all varargs sharing value
        }
    }
    else if (Is_Level_Style_Varargs_May_Fail(&L, vararg)) {
        //
        // "Ordinary" case... use the original level implied by the VARARGS!
        // (so long as it is still live on the stack)

        // The enfixed case always synthesizes an array to hold the evaluated
        // left hand side value.  (See notes on Is_Varargs_Enfix().)
        //
        assert(not Is_Varargs_Enfix(vararg));

        vararg_level = L;

        Option(const Element*) look = nullptr;
        if (not Is_Level_At_End(L))
            look = At_Level(L);

        if (Vararg_Op_If_No_Advance_Handled(
            out,
            op,
            look,
            Level_Specifier(L),
            pclass
        )){
            goto type_check_and_return;
        }

        // Note that evaluative cases here need Eval_Step_In_Sublevel(),
        // because a function is running and the level state can't be
        // overwritten by an arbitrary evaluation.
        //
        switch (pclass) {
        case PARAMCLASS_NORMAL: {
            Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;

            if (Eval_Step_In_Sublevel_Throws(out, L, flags))
                return true;
            break; }

        case PARAMCLASS_HARD:
            Literal_Next_In_Feed(out, L->feed);
            break;

        case PARAMCLASS_MEDIUM:  // !!! Review nuance
        case PARAMCLASS_SOFT:
            if (ANY_ESCAPABLE_GET(At_Level(L))) {
                if (Eval_Value_Throws(
                    out,
                    At_Level(L),
                    Level_Specifier(L)
                )){
                    return true;
                }
                Fetch_Next_In_Feed(L->feed);
            }
            else // not a soft-"exception" case, quote ordinarily
                Literal_Next_In_Feed(out, L->feed);
            break;

        default:
            fail ("Invalid variadic parameter class");
        }
    }
    else
        panic ("Malformed VARARG cell");

  type_check_and_return:;

    if (Is_Fresh(out))
        return false;

    if (op == VARARG_OP_TAIL_Q) {
        assert(Is_Logic(out));
        return false;
    }

    if (param and not Is_Barrier(out)) {
        if (not Typecheck_Coerce_Argument(param, out)) {
            //
            // !!! Array-based varargs only store the parameter list they are
            // stamped with, not the level.  This is because storing non-reified
            // types in payloads is unsafe...only safe to store Level* in a
            // binding.  So that means only one level can be pointed to per
            // vararg.  Revisit the question of how to give better errors.
            //
            if (not vararg_level)
                fail (out);

            fail (Error_Phase_Arg_Type(
                unwrap(vararg_level), key, param, Stable_Unchecked(out))
            );
        }
    }

    // Note: may be at end now, but reflect that at *next* call

    return false; // not thrown
}


//
//  MAKE_Varargs: C
//
Bounce MAKE_Varargs(
    Level* level_,
    Kind kind,
    Option(const Value*) parent,
    const Value* arg
){
    assert(kind == REB_VARARGS);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    // With MAKE VARARGS! on an ANY-ARRAY?, the array is the backing store
    // (shared) that the varargs interface cannot affect, but changes to
    // the array will change the varargs.
    //
    if (Any_Array(arg)) {
        //
        // Make a single-element array to hold a reference+index to the
        // incoming ANY-ARRAY?.  This level of indirection means all
        // VARARGS! copied from this will update their indices together.
        // By protocol, if the array is exhausted then the shared element
        // should be an END marker (not an array at its end)
        //
        Array* array1 = Alloc_Singular(NODE_FLAG_MANAGED);
        if (Cell_Series_Len_At(arg) == 0)
            Poison_Cell(Stub_Cell(array1));
        else
            Copy_Cell(Stub_Cell(array1), arg);

        Reset_Unquoted_Header_Untracked(TRACK(OUT), CELL_MASK_VARARGS);
        INIT_VAL_VARARGS_PHASE(OUT, nullptr);
        UNUSED(VAL_VARARGS_SIGNED_PARAM_INDEX(OUT));  // corrupts in C++11
        INIT_VAL_VARARGS_BINDING(OUT, array1);

        return OUT;
    }

    // !!! Permit FRAME! ?

    fail (Error_Bad_Make(REB_VARARGS, arg));
}


//
//  TO_Varargs: C
//
Bounce TO_Varargs(Level* level_, Kind kind, const Value* arg)
{
    assert(kind == REB_VARARGS);
    UNUSED(kind);

    return RAISE(arg);
}


//
//  REBTYPE: C
//
// Handles the very limited set of operations possible on a VARARGS!
// (evaluation state inspector/modifier during a DO).
//
REBTYPE(Varargs)
{
    Value* value = D_ARG(1);

    switch (Symbol_Id(verb)) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // already have `value`
        Option(SymId) property = Cell_Word_Id(ARG(property));

        switch (property) {
        case SYM_TAIL_Q: {
            if (Do_Vararg_Op_Maybe_End_Throws(
                OUT,
                VARARG_OP_TAIL_Q,
                value
            )){
                assert(false);
                return THROWN;
            }
            assert(Is_Logic(OUT));
            return OUT; }

        default:
            break;
        }

        break; }

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        if (not Is_Integer(picker))
            fail (picker);

        if (VAL_INT32(picker) != 1)
            fail (Error_Varargs_No_Look_Raw());

        if (Do_Vararg_Op_Maybe_End_Throws(
            OUT,
            VARARG_OP_FIRST,
            value
        )){
            assert(false); // VARARG_OP_FIRST can't throw
            return THROWN;
        }
        if (Is_Barrier(OUT))
            Init_Nulled(OUT);

        return OUT; }


    case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        UNUSED(PARAM(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(last))
            fail (Error_Varargs_Take_Last_Raw());

        if (not REF(part)) {
            if (Do_Vararg_Op_Maybe_End_Throws(
                OUT,
                VARARG_OP_TAKE,
                value
            )){
                return THROWN;
            }
            if (Is_Barrier(OUT))
                return RAISE(Error_Nothing_To_Take_Raw());
            return OUT;
        }

        StackIndex base = TOP_INDEX;

        if (not Is_Integer(ARG(part)))
            fail (PARAM(part));

        REBINT limit = VAL_INT32(ARG(part));
        if (limit < 0)
            limit = 0;

        while (limit-- > 0) {
            if (Do_Vararg_Op_Maybe_End_Throws(
                OUT,
                VARARG_OP_TAKE,
                value
            )){
                return THROWN;
            }
            if (Is_Barrier(OUT))
                break;
            Move_Cell(PUSH(), Decay_If_Unstable(OUT));
        }

        // !!! What if caller wanted a REB_GROUP, REB_PATH, or an /INTO?
        //
        return Init_Block(OUT, Pop_Stack_Values(base)); }

    default:
        break;
    }

    fail (UNHANDLED);
}


//
//  CT_Varargs: C
//
// Simple comparison function stub (required for every type--rules TBD for
// levels of "exactness" in equality checking, or sort-stable comparison.)
//
REBINT CT_Varargs(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);

    // !!! For the moment, say varargs are the same if they have the same
    // source feed from which the data comes.  (This check will pass even
    // expired varargs, because the expired stub should be kept alive as
    // long as its identity is needed).
    //
    if (VAL_VARARGS_BINDING(a) == VAL_VARARGS_BINDING(b))
        return 0;
    return VAL_VARARGS_BINDING(a) > VAL_VARARGS_BINDING(b) ? 1 : -1;
}


//
//  MF_Varargs: C
//
// The molding of a VARARGS! does not necessarily have complete information,
// because it doesn't want to perform evaluations...or advance any frame it
// is tied to.  However, a few things are knowable; such as if the varargs
// has reached its end, or if the frame the varargs is attached to is no
// longer on the stack.
//
void MF_Varargs(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form);

    Pre_Mold(mo, v);  // #[varargs! or make varargs!

    Append_Codepoint(mo->series, '[');

    ParamClass pclass;
    const Key* key;
    const Param* param = Param_For_Varargs_Maybe_Null(&key, v);
    if (param == NULL) {
        pclass = PARAMCLASS_HARD;
        Append_Ascii(mo->series, "???"); // never bound to an argument
    }
    else {
        Heart heart;
        bool quoted = false;
        switch ((pclass = Cell_ParamClass(param))) {
        case PARAMCLASS_NORMAL:
            heart = REB_WORD;
            break;

        case PARAMCLASS_HARD:
            heart = REB_WORD;
            quoted = true;
            break;

        case PARAMCLASS_MEDIUM:
            heart = REB_GET_WORD;
            quoted = true;
            break;

        case PARAMCLASS_SOFT:
            heart = REB_GET_WORD;
            break;

        default:
            panic (NULL);
        };

        DECLARE_ELEMENT (param_word);
        Init_Any_Word(param_word, heart, KEY_SYMBOL(key));
        if (quoted)
            Quotify(param_word, 1);
        Mold_Value(mo, param_word);
    }

    Append_Ascii(mo->series, " => ");

    Level* L;
    Element* shared;
    if (Is_Block_Style_Varargs(&shared, v)) {
        if (Is_Cell_Poisoned(shared))
            Append_Ascii(mo->series, "[]");
        else if (pclass == PARAMCLASS_HARD)
            Mold_Value(mo, shared); // full feed can be shown if hard quoted
        else
            Append_Ascii(mo->series, "[...]"); // can't look ahead
    }
    else if (Is_Level_Style_Varargs_Maybe_Null(&L, v)) {
        if (L == NULL)
            Append_Ascii(mo->series, "!!!");
        else if (Is_Feed_At_End(L->feed)) {
            Append_Ascii(mo->series, "[]");
        }
        else if (pclass == PARAMCLASS_HARD) {
            Append_Ascii(mo->series, "[");
            Mold_Value(mo, At_Feed(L->feed)); // one value shown if hard quoted
            Append_Ascii(mo->series, " ...]");
        }
        else
            Append_Ascii(mo->series, "[...]");
    }
    else
        assert(false);

    Append_Codepoint(mo->series, ']');

    End_Mold(mo);
}


//
//  variadic?: native [
//
//  "Returns TRUE if a frame may take a variable number of arguments"
//
//      return: [logic?]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(variadic_q)
{
    INCLUDE_PARAMS_OF_VARIADIC_Q;

    Action* action = VAL_ACTION(ARG(frame));

    const Key* key_tail;
    const Key* key = ACT_KEYS(&key_tail, action);
    const Value* param = ACT_PARAMS_HEAD(action);
    for (; key != key_tail; ++param, ++key) {
        if (Get_Parameter_Flag(param, VARIADIC))
            return Init_True(OUT);
    }

    return Init_False(OUT);
}
