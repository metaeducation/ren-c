//
//  File: %t-varargs.h
//  Summary: "Variadic Argument Type and Services"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// or arbitrary array of values.  All copied instances of a REB_VARARGS value
// remain in sync as values are TAKE-d out of them.  Once they report
// reaching a TAIL? they will always report TAIL?...until the call that
// spawned them is off the stack, at which point they will report an error.
//

#include "sys-core.h"


#define R_For_Vararg_End(op) \
    ((op) == VARARG_OP_TAIL_Q ? R_TRUE : R_VOID)


// Some VARARGS! are generated from a block with no frame, while others
// have a frame.  It would be inefficient to force the creation of a frame on
// each call for a BLOCK!-based varargs.  So rather than doing so, there's a
// prelude which sees if it can answer the current query just from looking one
// unit ahead.
//
inline static REB_R Vararg_Op_If_No_Advance(
    REBVAL *out,
    enum Reb_Vararg_Op op,
    const RELVAL *opt_look, // the first value in the varargs input
    REBSPC *specifier,
    enum Reb_Param_Class pclass
){
    if (opt_look == NULL)
        return R_For_Vararg_End(op); // exhausted

    assert(NOT_END(opt_look));

    if (IS_BAR(opt_look)) {
        //
        // Only hard quotes are allowed to see BAR! (and if they do, they
        // are *encouraged* to test the evaluated bit and error on literals,
        // unless they have a *really* good reason to do otherwise)
        //
        if (pclass == PARAM_CLASS_HARD_QUOTE) {
            if (op == VARARG_OP_TAIL_Q)
                return R_FALSE;
            if (op == VARARG_OP_FIRST) {
                Init_Bar(out);
                return R_OUT;
            }
            assert(op == VARARG_OP_TAKE);
            return R_UNHANDLED; // advance frame/array to consume BAR!
        }

        return R_For_Vararg_End(op); // simulate exhaustion for non hard quote
    }

    if (
        (pclass == PARAM_CLASS_NORMAL || pclass == PARAM_CLASS_TIGHT)
        && IS_WORD(opt_look)
    ){
        // When a variadic argument is being TAKE-n, deferred left hand side
        // argument needs to be seen as end of variadic input.  Otherwise,
        // `summation 1 2 3 |> 100` acts as `summation 1 2 (3 |> 100)`.
        // Deferred operators need to act somewhat as an expression barrier.
        //
        // Same rule applies for "tight" arguments, `sum 1 2 3 + 4` with
        // sum being variadic and tight needs to act as `(sum 1 2 3) + 4`
        //
        // Look ahead, and if actively bound see if it's to an enfix function
        // and the rules apply.  Note the raw check is faster, no need to
        // separately test for IS_END()

        const REBVAL *child_gotten = Get_Opt_Var_Else_End(opt_look, specifier);

        if (VAL_TYPE_OR_0(child_gotten) == REB_ACTION) {
            if (GET_VAL_FLAG(child_gotten, VALUE_FLAG_ENFIXED)) {
                if (
                    pclass == PARAM_CLASS_TIGHT
                    or GET_VAL_FLAG(child_gotten, ACTION_FLAG_DEFERS_LOOKBACK)
                ){
                    return R_For_Vararg_End(op);
                }
            }
        }
    }

    // The odd circumstances which make things simulate END--as well as an
    // actual END--are all taken care of, so we're not "at the TAIL?"
    //
    if (op == VARARG_OP_TAIL_Q)
        return R_FALSE;

    if (op == VARARG_OP_FIRST) {
        if (pclass != PARAM_CLASS_HARD_QUOTE)
            fail (Error_Varargs_No_Look_Raw()); // hard quote only

        Derelativize(out, opt_look, specifier);
        SET_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED);

        return R_OUT; // only a lookahead, no need to advance
    }

    return R_UNHANDLED; // must advance, may need to create a frame to do so
}


//
//  Do_Vararg_Op_May_Throw: C
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
// If op is VARARG_OP_TAIL_Q, then it will return R_TRUE or R_FALSE, and
// this case cannot return R_OUT_IS_THROWN.
//
// For other ops, it will return R_VOID if at the end of variadic input,
// or R_OUT if there is a value.  Note that since this can perform evaluations
// that R_OUT with out as void means an evaluation to void was performed,
// while R_VOID means it really physically hit the end of the frame/block.
//
// If an evaluation is involved, then R_OUT_IS_THROWN is possibly returned.
//
REB_R Do_Vararg_Op_May_Throw(
    REBVAL *out,
    RELVAL *vararg,
    enum Reb_Vararg_Op op
){
#if !defined(NDEBUG)
    if (op != VARARG_OP_TAIL_Q)
        TRASH_CELL_IF_DEBUG(out);
#endif

    const RELVAL *param; // for type checking
    enum Reb_Param_Class pclass;

    REBVAL *arg; // for updating VALUE_FLAG_UNEVALUATED

    REBARR *facade = vararg->payload.varargs.facade;
    if (facade == NULL) {
        //
        // A vararg created from a block AND never passed as an argument
        // so no typeset or quoting settings available.  Treat as "normal"
        // parameter.
        //
        assert(
            NOT_CELL(vararg->extra.binding)
            and not (vararg->extra.binding->header.bits & ARRAY_FLAG_VARLIST)
        );
        pclass = PARAM_CLASS_NORMAL;
        param = NULL; // doesn't correspond to a real varargs parameter
    }
    else {
        param = ARR_AT(facade, vararg->payload.varargs.param_offset + 1);
        pclass = VAL_PARAM_CLASS(param);
    }

    REB_R r;
    REBFRM *opt_vararg_frame;

    REBFRM *f;
    REBVAL *shared;
    if (Is_Block_Style_Varargs(&shared, vararg)) {
        //
        // We are processing an ANY-ARRAY!-based varargs, which came from
        // either a MAKE VARARGS! on an ANY-ARRAY! value -or- from a
        // MAKE ANY-ARRAY! on a varargs (which reified the varargs into an
        // array during that creation, flattening its entire output).

        opt_vararg_frame = NULL;
        arg = NULL; // no corresponding varargs argument either

        r = Vararg_Op_If_No_Advance(
            out,
            op,
            IS_END(shared) ? NULL : VAL_ARRAY_AT(shared), // NULL is protocol
            IS_END(shared) ? SPECIFIED : VAL_SPECIFIER(shared),
            pclass
        );

        if (r != R_UNHANDLED)
            goto type_check_and_return;

        switch (pclass) {
        case PARAM_CLASS_NORMAL:
        case PARAM_CLASS_TIGHT: {
            DECLARE_FRAME (f_temp);
            Push_Frame_At(
                f_temp,
                VAL_ARRAY(shared),
                VAL_INDEX(shared),
                VAL_SPECIFIER(shared),
                pclass == PARAM_CLASS_NORMAL
                    ? DO_FLAG_FULFILLING_ARG
                    : DO_FLAG_FULFILLING_ARG | DO_FLAG_NO_LOOKAHEAD
            );

            // Note: Do_Next_In_Subframe_Throws() is not needed here because
            // this is a single use frame, whose state can be overwritten.
            //
            if (Do_Next_In_Frame_Throws(out, f_temp)) {
                Drop_Frame(f_temp);
                return R_OUT_IS_THROWN;
            }

            if (FRM_AT_END(f_temp))
                SET_END(shared); // signal end to all varargs sharing value
            else {
                // The indexor is "prefetched", so though the temp_frame would
                // be ready to use again we're throwing it away, and need to
                // effectively "undo the prefetch" by taking it down by 1.
                //
                assert(f_temp->source.index > 0);
                VAL_INDEX(shared) = f_temp->source.index - 1; // all sharings
            }

            Drop_Frame(f_temp);
            break; }

        case PARAM_CLASS_HARD_QUOTE:
            Derelativize(out, VAL_ARRAY_AT(shared), VAL_SPECIFIER(shared));
            SET_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED);
            VAL_INDEX(shared) += 1;
            break;

        case PARAM_CLASS_SOFT_QUOTE:
            if (IS_QUOTABLY_SOFT(VAL_ARRAY_AT(shared))) {
                if (Eval_Value_Core_Throws(
                    out, VAL_ARRAY_AT(shared), VAL_SPECIFIER(shared)
                )){
                    return R_OUT_IS_THROWN;
                }
            }
            else { // not a soft-"exception" case, quote ordinarily
                Derelativize(out, VAL_ARRAY_AT(shared), VAL_SPECIFIER(shared));
                SET_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED);
            }
            VAL_INDEX(shared) += 1;
            break;

        default:
            fail ("Invalid variadic parameter class");
        }
    }
    else if (Is_Frame_Style_Varargs_May_Fail(&f, vararg)) {
        //
        // "Ordinary" case... use the original frame implied by the VARARGS!
        // (so long as it is still live on the stack)

        opt_vararg_frame = f;
        arg = FRM_ARG(f, vararg->payload.varargs.param_offset + 1);

        r = Vararg_Op_If_No_Advance(
            out,
            op,
            f->value, // NULL if FRM_AT_END()
            f->specifier,
            pclass
        );

        if (r != R_UNHANDLED)
            goto type_check_and_return;

        // Note that evaluative cases here need Do_Next_In_Subframe_Throws(),
        // because a function is running and the frame state can't be
        // overwritten by an arbitrary evaluation.
        //
        switch (pclass) {
        case PARAM_CLASS_NORMAL: {
            DECLARE_FRAME (child);
            if (Do_Next_In_Subframe_Throws(
                out,
                f,
                DO_FLAG_FULFILLING_ARG,
                child
            )){
                return R_OUT_IS_THROWN;
            }
            break; }

        case PARAM_CLASS_TIGHT: {
            DECLARE_FRAME (child);
            if (Do_Next_In_Subframe_Throws(
                out,
                f,
                DO_FLAG_FULFILLING_ARG | DO_FLAG_NO_LOOKAHEAD,
                child
            )){
                return R_OUT_IS_THROWN;
            }
            break; }

        case PARAM_CLASS_HARD_QUOTE:
            Quote_Next_In_Frame(out, f);
            break;

        case PARAM_CLASS_SOFT_QUOTE:
            if (IS_QUOTABLY_SOFT(f->value)) {
                if (Eval_Value_Core_Throws(out, f->value, f->specifier))
                    return R_OUT_IS_THROWN;

                Fetch_Next_In_Frame(f);
            }
            else // not a soft-"exception" case, quote ordinarily
                Quote_Next_In_Frame(out, f);
            break;

        default:
            fail ("Invalid variadic parameter class");
        }
    }
    else
        panic ("Malformed VARARG cell");

    r = R_OUT;

type_check_and_return:
    if (r != R_OUT) {
        assert(
            op == VARARG_OP_TAIL_Q ? r == R_TRUE || r == R_FALSE : r == R_VOID
        );
        return r;
    }

    assert(not THROWN(out)); // should have returned above

    if (param and not TYPE_CHECK(param, VAL_TYPE(out))) {
        assert(opt_vararg_frame != NULL); // !!! is this true?
        fail (Error_Arg_Type(opt_vararg_frame, param, VAL_TYPE(out)));
    }

    if (arg) {
        if (GET_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED))
            SET_VAL_FLAG(arg, VALUE_FLAG_UNEVALUATED);
        else
            CLEAR_VAL_FLAG(arg, VALUE_FLAG_UNEVALUATED);
    }

    return R_OUT; // may be at end now, but reflect that at *next* call
}


//
//  MAKE_Varargs: C
//
void MAKE_Varargs(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_VARARGS);
    UNUSED(kind);

    // With MAKE VARARGS! on an ANY-ARRAY!, the array is the backing store
    // (shared) that the varargs interface cannot affect, but changes to
    // the array will change the varargs.
    //
    if (ANY_ARRAY(arg)) {
        //
        // Make a single-element array to hold a reference+index to the
        // incoming ANY-ARRAY!.  This level of indirection means all
        // VARARGS! copied from this will update their indices together.
        // By protocol, if the array is exhausted then the shared element
        // should be an END marker (not an array at its end)
        //
        REBARR *array1 = Alloc_Singular_Array();
        if (IS_END(VAL_ARRAY_AT(arg)))
            SET_END(ARR_SINGLE(array1));
        else
            Move_Value(ARR_SINGLE(array1), arg);
        MANAGE_ARRAY(array1);

        RESET_VAL_HEADER(out, REB_VARARGS);
        out->payload.varargs.facade = NULL;
        UNUSED(out->payload.varargs.param_offset); // trashes in C++11 build
        INIT_BINDING(out, array1);

        return;
    }

    // !!! Permit FRAME! ?

    fail (Error_Bad_Make(REB_VARARGS, arg));
}


//
//  TO_Varargs: C
//
void TO_Varargs(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_VARARGS);
    UNUSED(kind);

    UNUSED(out);

    fail (Error_Invalid(arg));
}


//
//  PD_Varargs: C
//
// Implements the PICK* operation.
//
REB_R PD_Varargs(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    UNUSED(opt_setval);

    if (not IS_INTEGER(picker))
        fail (Error_Invalid(picker));

    if (VAL_INT32(picker) != 1)
        fail (Error_Varargs_No_Look_Raw());

    DECLARE_LOCAL (location);
    Move_Value(location, pvs->out);

    REB_R r = Do_Vararg_Op_May_Throw(pvs->out, location, VARARG_OP_FIRST);
    if (r == R_OUT_IS_THROWN)
        assert(FALSE); // VARARG_OP_FIRST can't throw
    else if (r == R_VOID)
        Init_Void(pvs->out);
    else
        assert(r == R_OUT);

    return R_OUT;
}


//
//  REBTYPE: C
//
// Handles the very limited set of operations possible on a VARARGS!
// (evaluation state inspector/modifier during a DO).
//
REBTYPE(Varargs)
{
    REBVAL *value = D_ARG(1);

    switch (verb) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // already have `value`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_TAIL_Q: {
            REBVAL *out = NULL; // won't write to `out`
            REB_R r = Do_Vararg_Op_May_Throw(out, value, VARARG_OP_TAIL_Q);
            assert(r == R_TRUE || r == R_FALSE); // cannot throw
            return r; }

        default:
            break;
        }

        break; }

    case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;

        UNUSED(PAR(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(last))
            fail (Error_Varargs_Take_Last_Raw());

        if (not REF(part))
            return Do_Vararg_Op_May_Throw(D_OUT, value, VARARG_OP_TAKE);

        REBDSP dsp_orig = DSP;

        REBINT limit;
        if (IS_INTEGER(ARG(limit))) {
            limit = VAL_INT32(ARG(limit));
            if (limit < 0)
                limit = 0;
        }
        else if (IS_BAR(ARG(limit))) {
            limit = 0; // not used, but avoid maybe uninitalized warning
        }
        else
            fail (Error_Invalid(ARG(limit)));

        while (limit-- > 0) {
            REB_R r = Do_Vararg_Op_May_Throw(D_OUT, value, VARARG_OP_TAKE);

            if (r == R_OUT_IS_THROWN)
                return R_OUT_IS_THROWN;
            if (r == R_VOID)
                break;
            assert(r == R_OUT);

            DS_PUSH(D_OUT);
        }

        // !!! What if caller wanted a REB_GROUP, REB_PATH, or an /INTO?
        //
        Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
        return R_OUT; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_VARARGS, verb));
}


//
//  CT_Varargs: C
//
// Simple comparison function stub (required for every type--rules TBD for
// levels of "exactness" in equality checking, or sort-stable comparison.)
//
REBINT CT_Varargs(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    UNUSED(mode);

    // !!! For the moment, say varargs are the same if they have the same
    // source feed from which the data comes.  (This check will pass even
    // expired varargs, because the expired stub should be kept alive as
    // long as its identity is needed).
    //
    if (Same_Binding(VAL_BINDING(a), VAL_BINDING(b)))
        return 1;
    return 0;
}


//
//  MF_Varargs: C
//
// !!! The molding behavior was implemented to help with debugging the type,
// but is not ready for prime-time.  Rather than risk crashing or presenting
// incomplete information, it's very minimal for now.  Review after the
// VARARGS! have stabilized somewhat just how much information can (or should)
// be given when printing these out (they should not "lookahead")
//
void MF_Varargs(REB_MOLD *mo, const RELVAL *v, REBOOL form) {
    UNUSED(form);

    assert(IS_VARARGS(v));

    Pre_Mold(mo, v);  // #[varargs! or make varargs!

    Append_Utf8_Codepoint(mo->series, '[');

    if (v->payload.varargs.facade == NULL) {
        Append_Unencoded(mo->series, "???");
    }
    else {
        REBCTX *context = CTX(v->extra.binding);
        REBFRM *param_frame = CTX_FRAME_IF_ON_STACK(context);

        if (param_frame == NULL) {
            Append_Unencoded(mo->series, "???");
        }
        else {
            const RELVAL *param
                = ACT_FACADE_HEAD(param_frame->phase)
                    + v->payload.varargs.param_offset;

            enum Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
            enum Reb_Kind kind;
            switch (pclass) {
                case PARAM_CLASS_NORMAL:
                    kind = REB_WORD;
                    break;

                case PARAM_CLASS_TIGHT:
                    kind = REB_ISSUE;
                    break;

                case PARAM_CLASS_HARD_QUOTE:
                    kind = REB_GET_WORD;
                    break;

                case PARAM_CLASS_SOFT_QUOTE:
                    kind = REB_LIT_WORD;
                    break;

                default:
                    panic (NULL);
            };

            // Note varargs_param is distinct from f->param!
            DECLARE_LOCAL (param_word);
            Init_Any_Word(
                param_word, kind, VAL_PARAM_SPELLING(param)
            );

            Mold_Value(mo, param_word);
        }
    }

    Append_Unencoded(mo->series, " <= ");

    REBFRM *f;

    if (IS_CELL(VAL_BINDING(v))) {
        f = cast(REBFRM*, VAL_BINDING(v));
        goto have_f;
    }
    else if (not (VAL_BINDING(v)->header.bits & ARRAY_FLAG_VARLIST)) {

        { // Just [...] for now
            Append_Unencoded(mo->series, "[...]");
            goto skip_complex_mold_for_now;
        }
        /*
        REBARR *array1 = ARR(VAL_BINDING(v));
        if (IS_END(ARR_SINGLE(array1)))
            Append_Unencoded(mo->series, "*exhausted*");
        else
            Mold_Value(mo, ARR_SINGLE(array1));
        */
    }
    else {
        f = CTX_FRAME_IF_ON_STACK(CTX(VAL_BINDING(v)));

        if (f == NULL) {
            Append_Unencoded(mo->series, "**unavailable: call ended **");
        }
        else {
        have_f:
            {// Just [...] for now
                Append_Unencoded(mo->series, "[...]");
                goto skip_complex_mold_for_now;
            }

            /*
            if (FRM_AT_END(f))
                Append_Unencoded(mo->series, "*exhausted*");
            else {
                Mold_Value(mo, f->value);

                if (FRM_IS_VALIST(f))
                    Append_Unencoded(mo->series, "*C varargs, pending*");
                else
                    Mold_Array_At(
                        mo, f->source.array, cast(REBCNT, f->source.index), NULL
                    );
            }
            */
        }
    }

skip_complex_mold_for_now:
    Append_Utf8_Codepoint(mo->series, ']');

    End_Mold(mo);
}
