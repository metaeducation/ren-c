//
//  File: %t-varargs.h
//  Summary: "Variadic Argument Type and Services"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Rebol Open Source Contributors
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
REBIXO Do_Vararg_Op_May_Throw(
    REBVAL *out,
    RELVAL *vararg,
    enum Reb_Vararg_Op op
) {
#if !defined(NDEBUG)
    if (op == VARARG_OP_TAIL_Q)
        assert(out == NULL); // not expecting return result
    else
        Init_End(out); // don't want trash in frame output on FAIL
#endif

    const RELVAL *param; // for type checking
    enum Reb_Param_Class pclass;

    REBVAL *arg; // for updating VALUE_FLAG_UNEVALUATED
    REBSTR *label;

    if (vararg->extra.binding == NULL) {
        //
        // Just a vararg created from a block, never passed as an argument
        // so no typeset or quoting settings available.  Treat as "normal"
        // parameter.
        //
        pclass = PARAM_CLASS_NORMAL;
        param = NULL; // doesn't correspond to a real varargs parameter
        arg = NULL; // no corresponding varargs argument either
        label = Canon(SYM___ANONYMOUS__);
    }
    else {
        REBCTX *context = AS_CONTEXT(vararg->extra.binding);
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

    if (op == VARARG_OP_FIRST && pclass != PARAM_CLASS_HARD_QUOTE)
        fail (Error_Varargs_No_Look_Raw()); // lookahead needs hard quote

    REBVAL *shared;

    REBFRM temp_frame;
    Prep_Global_Cell(&temp_frame.cell);

    REBFRM *f;

    if (Not_Ser_Flag(vararg->payload.varargs.feed, ARRAY_FLAG_VARLIST)) {
        REBARR *array1 = vararg->payload.varargs.feed;

        // We are processing an ANY-ARRAY!-based varargs, which came from
        // either a MAKE VARARGS! on an ANY-ARRAY! value -or- from a
        // MAKE ANY-ARRAY! on a varargs (which reified the varargs into an
        // array during that creation, flattening its entire output).
        //
        shared = KNOWN(ARR_HEAD(array1)); // 1 element, array or end mark

        if (IS_END(shared))
            return END_FLAG; // exhausted

        assert(IS_BLOCK(shared)); // holds index and data values (specified)

        // A proxy call frame is created to feed from the shared array, and
        // its index will be updated (or set to END when exhausted)

        if (VAL_INDEX(shared) >= ARR_LEN(VAL_ARRAY(shared))) {
            Init_End(shared); // input now exhausted, mark for shared instances
            return END_FLAG;
        }

        if (op == VARARG_OP_FIRST) {
            Derelativize(out, VAL_ARRAY_AT(shared), VAL_SPECIFIER(shared));
            return VA_LIST_FLAG;
        }

        // Fill in just enough enformation to call the FETCH-based routines

        Init_Endlike_Header(&temp_frame.flags, DO_FLAG_NORMAL);
        temp_frame.value = VAL_ARRAY_AT(shared);
        temp_frame.specifier = VAL_SPECIFIER(shared);
        temp_frame.source.array = VAL_ARRAY(shared);
        temp_frame.index = VAL_INDEX(shared) + 1;
        temp_frame.out = out;
        temp_frame.pending = NULL;
        temp_frame.gotten = END;

        f = &temp_frame;
    }
    else {
        REBCTX *context = AS_CONTEXT(vararg->payload.varargs.feed);

        // If the VARARGS! has a call frame, then ensure that the call frame
        // where the VARARGS! originated is still on the stack.
        //
        f = CTX_FRAME_IF_ON_STACK(context);
        if (f == NULL)
            fail (Error_Varargs_No_Stack_Raw());

        // "Ordinary" case... use the original frame implied by the VARARGS!
        // The Reb_Frame isn't a bad pointer, we checked FRAME! is stack-live.
        //
        if (IS_END(f->value))
            return END_FLAG;

        if (op == VARARG_OP_FIRST) {
            Derelativize(out, f->value, f->specifier);
            return VA_LIST_FLAG;
        }

        shared = NULL; // not used but avoid maybe uninitialized warning
    }

    // The invariant here is that `f` has been prepared for fetching/doing
    // and has at least one value in it.
    //
    assert(NOT_END(f->value));
    assert(op != VARARG_OP_FIRST);

    if (IS_BAR(f->value))
        return END_FLAG; // all functions, including varargs, stop at `|`

    // When a variadic argument is being TAKE-n, a deferred left hand side
    // argument needs to be seen as the end of variadic input.  Otherwise,
    // `summation 1 2 3 |> 100` would act as `summation 1 2 (3 |> 100)`.
    // A deferred operator needs to act somewhat as an expression barrier.
    //
    // The same rule applies for "tight" arguments, `sum 1 2 3 + 4` with
    // sum being variadic and tight needs to act as `(sum 1 2 3) + 4`
    //
    if (
        (pclass == PARAM_CLASS_NORMAL || pclass == PARAM_CLASS_TIGHT)
        && IS_WORD(f->value)
        && IS_WORD_BOUND(f->value)
    ){
        // !!! "f" frame is eval_type REB_FUNCTION and we can't disrupt that.
        // If we were going to reuse this fetch then we'd have to build a
        // child frame and call Do_Core(), not Do_Next_In_Frame_May_Throw()
        // because it would be child->eval_type and child->gotten we pre-set
        //
        REBVAL *child_gotten = Get_Var_Core(
            f->value,
            f->specifier,
            GETVAR_READ_ONLY
        );

        if (IS_FUNCTION(child_gotten)) {
            if (Get_Val_Flag(child_gotten, VALUE_FLAG_ENFIXED)) {
                if (pclass == PARAM_CLASS_TIGHT)
                    return END_FLAG;
                if (Get_Val_Flag(child_gotten, FUNC_FLAG_DEFERS_LOOKBACK_ARG))
                    return END_FLAG;
            }
        }
    }

    // Based on the quoting class of the parameter, fulfill the varargs from
    // whatever information was loaded into `c` as the "feed" for values.
    //
    switch (pclass) {
    case PARAM_CLASS_NORMAL: {
        if (op == VARARG_OP_TAIL_Q)
            return VA_LIST_FLAG;

        if (Do_Next_In_Subframe_Throws(out, f, DO_FLAG_FULFILLING_ARG))
            return THROWN_FLAG;
        break; }

    case PARAM_CLASS_TIGHT: {
        if (op == VARARG_OP_TAIL_Q)
            return VA_LIST_FLAG;

        if (Do_Next_In_Subframe_Throws(
            out,
            f,
            DO_FLAG_FULFILLING_ARG | DO_FLAG_NO_LOOKAHEAD
        )){
            return THROWN_FLAG;
        }
        break; }

    case PARAM_CLASS_HARD_QUOTE:
        if (op == VARARG_OP_TAIL_Q) return VA_LIST_FLAG;

        Quote_Next_In_Frame(out, f);
        break;

    case PARAM_CLASS_SOFT_QUOTE:
        if (
            IS_GROUP(f->value)
            || IS_GET_WORD(f->value)
            || IS_GET_PATH(f->value) // these 3 cases evaluate
        ) {
            if (op == VARARG_OP_TAIL_Q) return VA_LIST_FLAG;

            if (Eval_Value_Core_Throws(out, f->value, f->specifier))
                return THROWN_FLAG;

            Fetch_Next_In_Frame(f);
        }
        else { // not a soft-"exception" case, quote ordinarily
            if (op == VARARG_OP_TAIL_Q) return VA_LIST_FLAG;

            Quote_Next_In_Frame(out, f);
        }
        break;

    default:
        assert(FALSE);
    }

    if (arg) {
        if (Get_Val_Flag(out, VALUE_FLAG_UNEVALUATED))
            Set_Val_Flag(arg, VALUE_FLAG_UNEVALUATED);
        else
            Clear_Val_Flag(arg, VALUE_FLAG_UNEVALUATED);
    }

    assert(NOT(THROWN(out))); // should have returned above

    // If the `c` we were updating was the stack local call we created just
    // for this function, then the new index status would be lost when this
    // routine ended.  Update the indexor state in the sub_value array.
    //
    if (f == &temp_frame) {
        assert(ANY_ARRAY(shared));
        if (IS_END(f->value))
            Init_End(shared); // signal no more to all varargs sharing value
        else {
            // The indexor is "prefetched", so although the temp_frame would
            // be ready to use again we're throwing it away, and need to
            // effectively "undo the prefetch" by taking it down by 1.  The
            //
            assert(f->index > 0);
            VAL_INDEX(shared) = f->index - 1; // update seen by all sharings
        }
    }

    if (param && !TYPE_CHECK(param, VAL_TYPE(out)))
        fail (Error_Arg_Type(label, param, VAL_TYPE(out)));

    return VA_LIST_FLAG; // may be at end now, but reflect that at *next* call
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

        Reset_Val_Header(out, REB_VARARGS);
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
//  REBTYPE: C
//
// Handles the very limited set of operations possible on a VARARGS!
// (evaluation state inspector/modifier during a DO).
//
REBTYPE(Varargs)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBIXO indexor;

    switch (action) {
    case SYM_PICK_P: {
        if (NOT(IS_INTEGER(arg)))
            fail (arg);

        if (VAL_INT32(arg) != 1)
            fail (Error_Varargs_No_Look_Raw());

        indexor = Do_Vararg_Op_May_Throw(D_OUT, value, VARARG_OP_FIRST);
        assert(indexor == VA_LIST_FLAG || indexor == END_FLAG); // no throw
        if (indexor == END_FLAG)
            Init_Blank(D_OUT); // want to be consistent with TAKE

        return R_OUT;
    }

    case SYM_TAIL_Q: {
        indexor = Do_Vararg_Op_May_Throw(NULL, value, VARARG_OP_TAIL_Q);
        assert(indexor == VA_LIST_FLAG || indexor == END_FLAG); // no throw
        return indexor == END_FLAG ? R_TRUE : R_FALSE;
    }

    case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;

        REBDSP dsp_orig = DSP;

        UNUSED(PAR(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(last))
            fail (Error_Varargs_Take_Last_Raw());

        if (NOT(REF(part))) {
            indexor = Do_Vararg_Op_May_Throw(D_OUT, value, VARARG_OP_TAKE);
            if (indexor == THROWN_FLAG)
                return R_OUT_IS_THROWN;

            if (indexor == END_FLAG)
                Init_Void(D_OUT); // currently allowed even without an /OPT

            return R_OUT;
        }

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

        while (IS_BAR(ARG(limit)) || limit-- > 0) {
            indexor = Do_Vararg_Op_May_Throw(D_OUT, value, VARARG_OP_TAKE);
            if (indexor == THROWN_FLAG)
                return R_OUT_IS_THROWN;

            if (indexor == END_FLAG)
                break;

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
        REBCTX *context = AS_CONTEXT(v->extra.binding);
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

    if (Not_Ser_Flag(feed, ARRAY_FLAG_VARLIST)) {
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
        REBCTX *context = AS_CONTEXT(feed);
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
