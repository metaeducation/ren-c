//
//  file: %t-varargs.h
//  summary: "Variadic Argument Type and Services"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// The VARARGS! data type implements an abstraction layer over a call frame
// or arbitrary array of values.  All copied instances of a TYPE_VARARGS value
// remain in sync as values are TAKE-d out of them.  Once they report
// reaching a TAIL? they will always report TAIL?...until the call that
// spawned them is off the stack, at which point they will report an error.
//

#include "sys-core.h"


INLINE void Init_For_Vararg_End(Value* out, enum Reb_Vararg_Op op) {
    if (op == VARARG_OP_TAIL_Q)
        Init_Logic(out, true);
    else
        SET_END(out);
}


// Some VARARGS! are generated from a block with no frame, while others
// have a frame.  It would be inefficient to force the creation of a frame on
// each call for a BLOCK!-based varargs.  So rather than doing so, there's a
// prelude which sees if it can answer the current query just from looking one
// unit ahead.
//
INLINE bool Vararg_Op_If_No_Advance_Handled(
    Value* out,
    enum Reb_Vararg_Op op,
    const Cell* opt_look, // the first value in the varargs input
    Specifier* specifier,
    ParamClass pclass
){
    if (IS_END(opt_look)) {
        Init_For_Vararg_End(out, op); // exhausted
        return true;
    }

    if (pclass == PARAMCLASS_NORMAL and Is_Word(opt_look)) {
        //
        // When a variadic argument is being TAKE-n, deferred left hand side
        // argument needs to be seen as end of variadic input.  Otherwise,
        // `summation 1 2 3 |> 100` acts as `summation 1 2 (3 |> 100)`.
        // Deferred operators need to act somewhat as an expression barrier.
        //
        // Same rule applies for "tight" arguments, `sum 1 2 3 + 4` with
        // sum being variadic and tight needs to act as `(sum 1 2 3) + 4`
        //
        // Look ahead, and if actively bound see if it's to an infix function
        // and the rules apply.  Note the raw check is faster, no need to
        // separately test for IS_END()

        const Value* child_gotten = Try_Get_Opt_Var(opt_look, specifier);

        if (child_gotten and Type_Of(child_gotten) == TYPE_ACTION) {
            if (Get_Cell_Flag(child_gotten, INFIX_IF_ACTION)) {
                if (Get_Cell_Flag(child_gotten, DEFER_INFIX_IF_ACTION)) {
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
        Init_Logic(out, false);
        return true;
    }

    if (op == VARARG_OP_FIRST) {
        if (pclass != PARAMCLASS_HARD_QUOTE)
            panic (Error_Varargs_No_Look_Raw()); // hard quote only

        Derelativize(out, opt_look, specifier);

        return true; // only a lookahead, no need to advance
    }

    return false; // must advance, may need to create a frame to do so
}


//
//  Do_Vararg_Op_Maybe_End_Throws: C
//
// Service routine for working with a VARARGS!.  Supports TAKE-ing or just
// returning whether it's at the end or not.  The TAKE is not actually a
// destructive operation on underlying data--merely a semantic chosen to
// convey feeding forward with no way to go back.
//
// Whether the parameter is quoted or evaluated is determined by the typeset
// information of the `param`.  The typeset in the param is also used to
// check the result, and if an error is delivered it will use the name of
// the parameter symbol in the panic() message.
//
// If op is VARARG_OP_TAIL_Q, then it will return ~okay~ or ~nulled~
// and this case cannot return a thrown value.
//
// For other ops, it will return END_NODE if at the end of variadic input,
// or OUT if there is a value.
//
// If an evaluation is involved, then a thrown value is possibly returned.
//
bool Do_Vararg_Op_Maybe_End_Throws(
    Value* out,
    const Cell* vararg,
    enum Reb_Vararg_Op op
){
    Erase_Cell(out);

    const Cell* param = Param_For_Varargs_Maybe_Null(vararg);
    ParamClass pclass =
        (param == nullptr) ? PARAMCLASS_HARD_QUOTE :  Cell_Parameter_Class(param);

    Level* opt_vararg_level;

    Level* L;
    Value* shared;
    if (Is_Block_Style_Varargs(&shared, vararg)) {
        //
        // We are processing an ANY-ARRAY!-based varargs, which came from
        // either a MAKE VARARGS! on an ANY-ARRAY! value -or- from a
        // MAKE ANY-ARRAY! on a varargs (which reified the varargs into an
        // array during that creation, flattening its entire output).

        opt_vararg_level = nullptr;

        if (Vararg_Op_If_No_Advance_Handled(
            out,
            op,
            IS_END(shared) ? END_NODE : List_At(shared),
            IS_END(shared) ? SPECIFIED : VAL_SPECIFIER(shared),
            pclass
        )){
            goto type_check_and_return;
        }

        if (Get_Cell_Flag(vararg, VARARGS_INFIX)) {
            //
            // See notes on CELL_FLAG_VARARGS_INFIX about how the left hand side
            // is synthesized into an array-style varargs with either 0 or
            // 1 item to be taken.  But any evaluation has already happened
            // before the TAKE.  So although we honor the pclass to disallow
            // TAIL? or FIRST testing on evaluative parameters, we don't
            // want to double evaluation...so return that single element.
            //
            Value* single = KNOWN(ARR_SINGLE(Cell_Array(shared)));
            Copy_Cell(out, single);
            SET_END(shared);
            goto type_check_and_return;
        }

        switch (pclass) {
        case PARAMCLASS_NORMAL: {
            DECLARE_LEVEL (L_temp);
            Push_Level_At(
                L_temp,
                Cell_Array(shared),
                VAL_INDEX(shared),
                VAL_SPECIFIER(shared),
                pclass == PARAMCLASS_NORMAL
                    ? EVAL_FLAG_FULFILLING_ARG
                    : EVAL_FLAG_FULFILLING_ARG | EVAL_FLAG_NO_LOOKAHEAD
            );

            // Note: Eval_Step_In_Subframe_Throws() is not needed here because
            // this is a single use frame, whose state can be overwritten.
            //
            if (Eval_Step_Throws(SET_END(out), L_temp)) {
                Abort_Level(L_temp);
                return true;
            }

            if (IS_END(L_temp->value)) {
                SET_END(shared);
            }
            else {
                // The indexor is "prefetched", so though the temp_frame would
                // be ready to use again we're throwing it away, and need to
                // effectively "undo the prefetch" by taking it down by 1.
                //
                assert(L_temp->source->index > 0);
                VAL_INDEX(shared) = L_temp->source->index - 1; // all sharings
            }

            Drop_Level(L_temp);
            break; }

        case PARAMCLASS_HARD_QUOTE:
            Derelativize(out, List_At(shared), VAL_SPECIFIER(shared));
            VAL_INDEX(shared) += 1;
            break;

        case PARAMCLASS_SOFT_QUOTE:
            if (IS_QUOTABLY_SOFT(List_At(shared))) {
                if (Eval_Value_Core_Throws(
                    out, List_At(shared), VAL_SPECIFIER(shared)
                )){
                    return true;
                }
            }
            else { // not a soft-"exception" case, quote ordinarily
                Derelativize(out, List_At(shared), VAL_SPECIFIER(shared));
            }
            VAL_INDEX(shared) += 1;
            break;

        default:
            panic ("Invalid variadic parameter class");
        }

        if (NOT_END(shared) && VAL_INDEX(shared) >= VAL_LEN_HEAD(shared))
            SET_END(shared); // signal end to all varargs sharing value
    }
    else if (Is_Level_Style_Varargs_May_Panic(&L, vararg)) {
        //
        // "Ordinary" case... use the original frame implied by the VARARGS!
        // (so long as it is still live on the stack)

        // The infixed case always synthesizes an array to hold the evaluated
        // left hand side value.  (See notes on CELL_FLAG_VARARGS_INFIX.)
        //
        assert(Not_Cell_Flag(vararg, VARARGS_INFIX));

        opt_vararg_level = L;

        if (Vararg_Op_If_No_Advance_Handled(
            out,
            op,
            L->value, // might be END
            L->specifier,
            pclass
        )){
            goto type_check_and_return;
        }

        // Note that evaluative cases here need Eval_Step_In_Subframe_Throws(),
        // because a function is running and the frame state can't be
        // overwritten by an arbitrary evaluation.
        //
        switch (pclass) {
        case PARAMCLASS_NORMAL: {
            DECLARE_SUBLEVEL (child, L);
            if (Eval_Step_In_Subframe_Throws(
                SET_END(out),
                L,
                EVAL_FLAG_FULFILLING_ARG,
                child
            )){
                return true;
            }
            L->gotten = nullptr; // cache must be forgotten...
            break; }

        case PARAMCLASS_HARD_QUOTE:
            Quote_Next_In_Level(out, L);
            break;

        case PARAMCLASS_SOFT_QUOTE:
            if (IS_QUOTABLY_SOFT(L->value)) {
                if (Eval_Value_Core_Throws(
                    SET_END(out),
                    L->value,
                    L->specifier
                )){
                    return true;
                }
                Fetch_Next_In_Level(nullptr, L);
            }
            else // not a soft-"exception" case, quote ordinarily
                Quote_Next_In_Level(out, L);
            break;

        default:
            panic ("Invalid variadic parameter class");
        }
    }
    else
        crash ("Malformed VARARG cell");

  type_check_and_return:;

    if (IS_END(out))
        return false;

    if (op == VARARG_OP_TAIL_Q) {
        assert(Is_Logic(out));
        return false;
    }

    if (param and not Typeset_Check(param, Type_Of(out))) {
        //
        // !!! Array-based varargs only store the parameter list they are
        // stamped with, not the frame.  This is because storing non-reified
        // types in payloads is unsafe...only safe to store Level* in a
        // binding.  So that means only one frame can be pointed to per
        // vararg.  Revisit the question of how to give better errors.
        //
        if (opt_vararg_level == nullptr)
            panic (Error_Invalid(out));

        panic (Error_Arg_Type(opt_vararg_level, param, Type_Of(out)));
    }

    // Note: may be at end now, but reflect that at *next* call

    return false; // not thrown
}


//
//  MAKE_Varargs: C
//
Bounce MAKE_Varargs(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_VARARGS);
    UNUSED(kind);

    // With MAKE VARARGS! on an ANY-ARRAY!, the array is the backing store
    // (shared) that the varargs interface cannot affect, but changes to
    // the array will change the varargs.
    //
    if (Any_List(arg)) {
        //
        // Make a single-element array to hold a reference+index to the
        // incoming ANY-ARRAY!.  This level of indirection means all
        // VARARGS! copied from this will update their indices together.
        // By protocol, if the array is exhausted then the shared element
        // should be an END marker (not an array at its end)
        //
        Array* array1 = Alloc_Singular(NODE_FLAG_MANAGED);
        if (IS_END(List_At(arg)))
            SET_END(ARR_SINGLE(array1));
        else
            Copy_Cell(ARR_SINGLE(array1), arg);

        RESET_CELL(out, TYPE_VARARGS);
        out->payload.varargs.phase = nullptr;
        UNUSED(out->payload.varargs.param_offset); // trashes in C++11 build
        INIT_BINDING(out, array1);

        return out;
    }

    // !!! Permit FRAME! ?

    panic (Error_Bad_Make(TYPE_VARARGS, arg));
}


//
//  TO_Varargs: C
//
Bounce TO_Varargs(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_VARARGS);
    UNUSED(kind);

    UNUSED(out);

    panic (Error_Invalid(arg));
}


//
//  PD_Varargs: C
//
// Implements the PICK* operation.
//
Bounce PD_Varargs(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    UNUSED(opt_setval);

    if (not Is_Integer(picker))
        panic (Error_Invalid(picker));

    if (VAL_INT32(picker) != 1)
        panic (Error_Varargs_No_Look_Raw());

    DECLARE_VALUE (location);
    Copy_Cell(location, pvs->out);

    if (Do_Vararg_Op_Maybe_End_Throws(
        pvs->out,
        location,
        VARARG_OP_FIRST
    )){
        assert(false); // VARARG_OP_FIRST can't throw
        return BOUNCE_THROWN;
    }

    if (IS_END(pvs->out))
        Init_Endish_Nulled(pvs->out);

    return pvs->out;
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

    switch (Word_Id(verb)) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(VALUE)); // already have `value`
        Option(SymId) property = Word_Id(ARG(PROPERTY));
        assert(property != SYM_0);

        switch (property) {
        case SYM_TAIL_Q: {
            if (Do_Vararg_Op_Maybe_End_Throws(
                OUT,
                value,
                VARARG_OP_TAIL_Q
            )){
                assert(false);
                return BOUNCE_THROWN;
            }
            assert(Is_Logic(OUT));
            return OUT; }

        default:
            break;
        }

        break; }

    case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        UNUSED(PARAM(SERIES));
        if (Bool_ARG(DEEP))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(LAST))
            panic (Error_Varargs_Take_Last_Raw());

        if (not Bool_ARG(PART)) {
            if (Do_Vararg_Op_Maybe_End_Throws(
                OUT,
                value,
                VARARG_OP_TAKE
            )){
                return BOUNCE_THROWN;
            }
            if (IS_END(OUT))
                return Init_Endish_Nulled(OUT);
            return OUT;
        }

        StackIndex base = TOP_INDEX;

        REBINT limit;
        if (Is_Integer(ARG(LIMIT))) {
            limit = VAL_INT32(ARG(LIMIT));
            if (limit < 0)
                limit = 0;
        }
        else
            panic (Error_Invalid(ARG(LIMIT)));

        while (limit-- > 0) {
            if (Do_Vararg_Op_Maybe_End_Throws(
                OUT,
                value,
                VARARG_OP_TAKE
            )){
                return BOUNCE_THROWN;
            }
            if (IS_END(OUT))
                break;
            Copy_Cell(PUSH(), OUT);
        }

        // !!! What if caller wanted a TYPE_GROUP, TYPE_PATH, or an /INTO?
        //
        return Init_Block(OUT, Pop_Stack_Values(base)); }

    default:
        break;
    }

    panic (Error_Illegal_Action(TYPE_VARARGS, verb));
}


//
//  CT_Varargs: C
//
// Simple comparison function stub (required for every type--rules TBD for
// levels of "exactness" in equality checking, or sort-stable comparison.)
//
REBINT CT_Varargs(const Cell* a, const Cell* b, REBINT mode)
{
    UNUSED(mode);

    // !!! For the moment, say varargs are the same if they have the same
    // source feed from which the data comes.  (This check will pass even
    // expired varargs, because the expired stub should be kept alive as
    // long as its identity is needed).
    //
    if (VAL_BINDING(a) == VAL_BINDING(b))
        return 1;
    return 0;
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
void MF_Varargs(Molder* mo, const Cell* v, bool form) {
    UNUSED(form);

    Begin_Non_Lexical_Mold(mo, v);  // #[varargs! or make varargs!

    Append_Codepoint(mo->utf8flex, '[');

    ParamClass pclass;
    const Cell* param = Param_For_Varargs_Maybe_Null(v);
    if (param == nullptr) {
        pclass = PARAMCLASS_HARD_QUOTE;
        Append_Unencoded(mo->utf8flex, "???"); // never bound to an argument
    }
    else {
        enum Reb_Kind kind;
        switch ((pclass = Cell_Parameter_Class(param))) {
        case PARAMCLASS_NORMAL:
            kind = TYPE_WORD;
            break;

        case PARAMCLASS_HARD_QUOTE:
            kind = TYPE_GET_WORD;
            break;

        case PARAMCLASS_SOFT_QUOTE:
            kind = TYPE_LIT_WORD;
            break;

        default:
            crash (nullptr);
        };

        DECLARE_VALUE (param_word);
        Init_Any_Word(param_word, kind, Cell_Parameter_Symbol(param));
        Mold_Value(mo, param_word);
    }

    Append_Unencoded(mo->utf8flex, " => ");

    Option(Level*) L;
    Value* shared;
    if (Is_Block_Style_Varargs(&shared, v)) {
        if (IS_END(shared))
            Append_Unencoded(mo->utf8flex, "[]");
        else if (pclass == PARAMCLASS_HARD_QUOTE)
            Mold_Value(mo, shared); // full feed can be shown if hard quoted
        else
            Append_Unencoded(mo->utf8flex, "[...]"); // can't look ahead
    }
    else if (Is_Level_Style_Varargs_Maybe_Null(&L, v)) {
        if (not L)
            Append_Unencoded(mo->utf8flex, "!!!");
        else if (IS_END((unwrap L)->value))
            Append_Unencoded(mo->utf8flex, "[]");
        else if (pclass == PARAMCLASS_HARD_QUOTE) {
            Append_Unencoded(mo->utf8flex, "[");
            Mold_Value(mo, (unwrap L)->value); // hard quote can show one
            Append_Unencoded(mo->utf8flex, " ...]");
        }
        Append_Unencoded(mo->utf8flex, "[...]");
    }
    else
        assert(false);

    Append_Codepoint(mo->utf8flex, ']');

    End_Non_Lexical_Mold(mo);
}
