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
    const RELVAL *look,
    REBSPC *specifier,
    enum Reb_Param_Class pclass
){
    if (IS_END(look))
        return R_For_Vararg_End(op); // exhausted

    if (IS_BAR(look)) {
        //
        // Only hard quotes are allowed to see BAR! (and if they do, they
        // are *encouraged* to test the evaluated bit and error on literals,
        // unless they have a *really* good reason to do otherwise)
        //
        if (pclass == PARAM_CLASS_HARD_QUOTE) {
            if (op == VARARG_OP_TAIL_Q)
                return R_FALSE;
            if (op == VARARG_OP_FIRST) {
                SET_BAR(out);
                return R_OUT;
            }
            assert(op == VARARG_OP_TAKE);
            return R_UNHANDLED; // advance frame/array to consume BAR!
        }

        return R_For_Vararg_End(op);
    }

    if (
        (pclass == PARAM_CLASS_NORMAL || pclass == PARAM_CLASS_TIGHT)
        && IS_WORD(look)
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

        const REBVAL *child_gotten = Get_Opt_Var_Else_End(look, specifier);

        if (VAL_TYPE_OR_0(child_gotten) == REB_FUNCTION) {
            if (GET_VAL_FLAG(child_gotten, VALUE_FLAG_ENFIXED)) {
                if (
                    pclass == PARAM_CLASS_TIGHT
                    || GET_VAL_FLAG(child_gotten, FUNC_FLAG_DEFERS_LOOKBACK)
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

        Derelativize(out, look, specifier);
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
// * returns THROWN_FLAG if it takes from an evaluating vararg that throws
//
// * returns END_FLAG if it reaches the end of an entire input chain
//
// * returns VA_LIST_FLAG if the input is not exhausted
//
// Note: Returning VA_LIST_FLAG is probably a lie, since the odds of the
// underlying varargs being from a FRAME! running on a C `va_list` aren't
// necessarily that high.  For now it is a good enough signal simply because
// it is not an index number, so it is an opaque way of saying "there is
// still more data"--and it's the same type as END_FLAG and THROWN_FLAG.
//
REB_R Do_Vararg_Op_May_Throw(
    REBVAL *out,
    RELVAL *vararg,
    enum Reb_Vararg_Op op
) {
    assert(IS_END(out));

    const RELVAL *param; // for type checking
    enum Reb_Param_Class pclass;

    REBVAL *arg; // for updating VALUE_FLAG_UNEVALUATED
    REBSTR *label;

    if (vararg->extra.binding == NULL) {
        //
        // A vararg created from a block AND never passed as an argument
        // so no typeset or quoting settings available.  Treat as "normal"
        // parameter.
        //
        assert(
            NOT_SER_FLAG(
                vararg->payload.varargs.feed, ARRAY_FLAG_VARLIST
            )
        );
        pclass = PARAM_CLASS_NORMAL;
        param = NULL; // doesn't correspond to a real varargs parameter
        arg = NULL; // no corresponding varargs argument either
        label = Canon(SYM___ANONYMOUS__);
    }
    else {
        REBCTX *context = CTX(vararg->extra.binding);
        REBFRM *param_frame = CTX_FRAME_IF_ON_STACK(context);

        // If the VARARGS! has a call frame, then ensure that the call frame
        // where the VARARGS! originated is still on the stack.
        //
        if (param_frame == NULL)
            fail (Error_Varargs_No_Stack_Raw());

        param = FUNC_FACADE_HEAD(param_frame->phase)
            + vararg->payload.varargs.param_offset;
        pclass = VAL_PARAM_CLASS(param);

        arg = param_frame->args_head + vararg->payload.varargs.param_offset;

        label = FRM_LABEL(param_frame);
    }

    REB_R r;

    if (NOT_SER_FLAG(vararg->payload.varargs.feed, ARRAY_FLAG_VARLIST)) {
        //
        // We are processing an ANY-ARRAY!-based varargs, which came from
        // either a MAKE VARARGS! on an ANY-ARRAY! value -or- from a
        // MAKE ANY-ARRAY! on a varargs (which reified the varargs into an
        // array during that creation, flattening its entire output).

        REBARR *array1 = vararg->payload.varargs.feed;
        REBVAL *shared = KNOWN(ARR_HEAD(array1));

        assert(IS_END(shared) || (IS_BLOCK(shared) && ARR_LEN(array1) == 1));

        r = Vararg_Op_If_No_Advance(
            out,
            op,
            IS_END(shared) ? END : VAL_ARRAY_AT(shared),
            IS_END(shared) ? SPECIFIED : VAL_SPECIFIER(shared),
            pclass
        );

        if (r != R_UNHANDLED)
            goto type_check_and_return;

        switch (pclass) {
        case PARAM_CLASS_NORMAL:
        case PARAM_CLASS_TIGHT: {
            DECLARE_FRAME (f);
            Push_Frame_At(
                f,
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
            if (Do_Next_In_Frame_Throws(out, f)) {
                Drop_Frame(f);
                return R_OUT_IS_THROWN;
            }

            if (IS_END(f->value))
                SET_END(shared); // signal end to all varargs sharing value
            else {
                // The indexor is "prefetched", so though the temp_frame would
                // be ready to use again we're throwing it away, and need to
                // effectively "undo the prefetch" by taking it down by 1.
                //
                assert(f->index > 0);
                VAL_INDEX(shared) = f->index - 1; // seen by all sharings
            }

            Drop_Frame(f);
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
    else {
        // "Ordinary" case... use the original frame implied by the VARARGS!
        // (so long as it is still live on the stack)

        REBCTX *context = CTX(vararg->payload.varargs.feed);
        REBFRM *f = CTX_FRAME_IF_ON_STACK(context);
        if (f == NULL)
            fail (Error_Varargs_No_Stack_Raw());

        r = Vararg_Op_If_No_Advance(
            out,
            op,
            f->value,
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
        case PARAM_CLASS_NORMAL:
            if (Do_Next_In_Subframe_Throws(out, f, DO_FLAG_FULFILLING_ARG))
                return R_OUT_IS_THROWN;
            break;

        case PARAM_CLASS_TIGHT:
            if (Do_Next_In_Subframe_Throws(
                out,
                f,
                DO_FLAG_FULFILLING_ARG | DO_FLAG_NO_LOOKAHEAD
            )){
                return R_OUT_IS_THROWN;
            }
            break;

        case PARAM_CLASS_HARD_QUOTE:
            Quote_Next_In_Frame(out, f);
            break;

        case PARAM_CLASS_SOFT_QUOTE:
            if (IS_QUOTABLY_SOFT(f->value)) {
                if (Eval_Value_Core_Throws(out, f->value, f->specifier))
                    return R_OUT_IS_THROWN;

                Fetch_Next_In_Frame(f);
            }
            else { // not a soft-"exception" case, quote ordinarily
                Quote_Next_In_Frame(out, f);
            }
            break;

        default:
            fail ("Invalid variadic parameter class");
        }
    }

    r = R_OUT;

type_check_and_return:
    if (r != R_OUT) {
        assert(
            op == VARARG_OP_TAIL_Q ? r == R_TRUE || r == R_FALSE : r == R_VOID
        );
        return r;
    }

    assert(NOT(THROWN(out))); // should have returned above

    if (param && NOT(TYPE_CHECK(param, VAL_TYPE(out))))
        fail (Error_Arg_Type(label, param, VAL_TYPE(out)));

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
        //
        REBARR *array1 = Alloc_Singular_Array();
        Move_Value(ARR_HEAD(array1), arg);
        MANAGE_ARRAY(array1);

        VAL_RESET_HEADER(out, REB_VARARGS);
        out->extra.binding = NULL;
    #if !defined(NDEBUG)
        out->payload.varargs.param_offset = -1020;
    #endif
        out->payload.varargs.feed = array1;

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

    fail (arg);
}


//
//  PD_Varargs: C
//
// Implements the PICK* operation.
//
REBINT PD_Varargs(REBPVS *pvs)
{
    if (NOT(IS_INTEGER(pvs->picker)))
        fail (pvs->picker);

    if (VAL_INT32(pvs->picker) != 1)
        fail (Error_Varargs_No_Look_Raw());

    DECLARE_LOCAL (specific);
    Derelativize(specific, pvs->value, pvs->value_specifier);

    REB_R r = Do_Vararg_Op_May_Throw(pvs->store, specific, VARARG_OP_FIRST);
    if (r == R_OUT_IS_THROWN)
        assert(FALSE); // VARARG_OP_FIRST can't throw
    else if (r == R_VOID)
        SET_VOID(pvs->store);
    else
        assert(r == R_OUT);

    return PE_USE_STORE;
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

    switch (action) {
    // !!! SYM_PICK_P moved into PD_Varargs functionality, which PICK* uses

    case SYM_TAIL_Q: {
        REB_R r = Do_Vararg_Op_May_Throw(
            m_cast(REBVAL*, END), value, VARARG_OP_TAIL_Q // won't write `out`
        );
        assert(r == R_TRUE || r == R_FALSE); // cannot throw
        return r; }

    case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;

        UNUSED(PAR(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(last))
            fail (Error_Varargs_Take_Last_Raw());

        if (NOT(REF(part)))
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
            fail (ARG(limit));

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
        return R_OUT;
    }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_VARARGS, action));
}


//
//  CT_Varargs: C
//
// Simple comparison function stub (required for every type--rules TBD for
// levels of "exactness" in equality checking, or sort-stable comparison.)
//
REBINT CT_Varargs(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    cast(void, mode);

    // !!! For the moment, say varargs are the same if they have the same
    // source feed from which the data comes.  (This check will pass even
    // expired varargs, because the expired stub should be kept alive as
    // long as its identity is needed).
    //
    if (a->payload.varargs.feed == b->payload.varargs.feed)
        return 1;
    return 0;
}


//
//  Mold_Varargs: C
//
// !!! The molding behavior was implemented to help with debugging the type,
// but is not ready for prime-time.  Rather than risk crashing or presenting
// incomplete information, it's very minimal for now.  Review after the
// VARARGS! have stabilized somewhat just how much information can (or should)
// be given when printing these out (they should not "lookahead")
//
void Mold_Varargs(const REBVAL *v, REB_MOLD *mold) {
    assert(IS_VARARGS(v));

    Pre_Mold(v, mold);  // #[varargs! or make varargs!

    Append_Codepoint_Raw(mold->series, '[');

    if (v->extra.binding == NULL) {
        Append_Unencoded(mold->series, "???");
    }
    else {
        REBCTX *context = CTX(v->extra.binding);
        REBFRM *param_frame = CTX_FRAME_IF_ON_STACK(context);

        if (param_frame == NULL) {
            Append_Unencoded(mold->series, "???");
        }
        else {
            const RELVAL *param
                = FUNC_FACADE_HEAD(param_frame->phase)
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

            Mold_Value(mold, param_word, TRUE);
        }
    }

    Append_Unencoded(mold->series, " <= ");

    REBARR *feed = v->payload.varargs.feed;

    if (NOT_SER_FLAG(feed, ARRAY_FLAG_VARLIST)) {
        REBARR *array1 = feed;

        { // Just [...] for now
            Append_Unencoded(mold->series, "[...]");
            goto skip_complex_mold_for_now;
        }

        if (IS_END(ARR_HEAD(array1)))
            Append_Unencoded(mold->series, "*exhausted*");
        else
            Mold_Value(mold, ARR_HEAD(array1), TRUE);
    }
    else if (NOT(IS_ARRAY_MANAGED(feed))) {
        //
        // This can happen if you internally try and PROBE() a varargs
        // item that is residing in the argument slots for a function,
        // while that function is still fulfilling its arguments.
        //
        Append_Unencoded(mold->series, "** varargs frame not fulfilled");
    }
    else {
        REBCTX *context = CTX(feed);
        REBFRM *f = CTX_FRAME_IF_ON_STACK(context);

        if (f == NULL) {
            Append_Unencoded(mold->series, "**unavailable: call ended **");
        }
        else {
            {// Just [...] for now
                Append_Unencoded(mold->series, "[...]");
                goto skip_complex_mold_for_now;
            }

            if (IS_END(f->value))
                Append_Unencoded(mold->series, "*exhausted*");
            else {
                Mold_Value(mold, f->value, TRUE);

                if (f->flags.bits & DO_FLAG_VA_LIST)
                    Append_Unencoded(mold->series, "*C varargs, pending*");
                else
                    Mold_Array_At(
                        mold, f->source.array, cast(REBCNT, f->index), NULL
                    );
            }
        }
    }

skip_complex_mold_for_now:
    Append_Codepoint_Raw(mold->series, ']');

    End_Mold(mold);
}
