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
// Experimental design being incorporated for testing.  For working notes,
// see the Ren-C Trello:
//
// https://trello.com/c/Y17CEywN
//
// The VARARGS! data type implements an abstraction layer over a call frame
// or arbitrary array of values.  All copied instances of a REB_VARARG value
// remain in sync as values are TAKE-d out of them, and once they report
// reaching a TAIL? they will always report TAIL?...until the call that
// spawned them is off the stack, at which point they will report an error.
//

#include "sys-core.h"


//
//  Do_Vararg_Op_Core: C
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
REBIXO Do_Vararg_Op_Core(
    REBVAL *out,
    REBARR *feed, // may be varlist or 1-element-long array w/shared value
    const RELVAL *param,
    REBVAL *arg, // for updating VALUE_FLAG_EVALUATED
    REBSTR *opt_label, // symbol of the function invocation param belongs to
    enum Reb_Vararg_Op op
) {
    struct Reb_Frame *f;
    REBIXO indexor = VA_LIST_FLAG;

    struct Reb_Frame temp_frame;

    REBARR **subfeed_addr;
    REBVAL *shared;

    enum Reb_Param_Class pclass = VAL_PARAM_CLASS(param);

    assert(LOGICAL(out == NULL) == LOGICAL(op == VARARG_OP_TAIL_Q));

    if (op == VARARG_OP_FIRST && pclass != PARAM_CLASS_HARD_QUOTE)
        fail (Error(RE_VARARGS_NO_LOOK)); // lookback needs hard quote

    // If the VARARGS! has a call frame, then ensure that the call frame where
    // the VARARGS! originated is still on the stack.
    //
    // !!! This test is not good enough for "durables", and if FRAME! can be
    // reused on the stack then it could still be alive even though the
    // call pointer it first ran with is dead.  There needs to be a solution
    // for other reasons, so use that solution when it's ready.
    //
    if (GET_ARR_FLAG(feed, ARRAY_FLAG_CONTEXT_VARLIST)) {
        if (
            GET_ARR_FLAG(feed, CONTEXT_FLAG_STACK)
            && !GET_ARR_FLAG(feed, SERIES_FLAG_ACCESSIBLE)
        ) {
            fail (Error(RE_VARARGS_NO_STACK));
        }

        f = CTX_FRAME(AS_CONTEXT(feed));

        // Take label symbol from context if it hasn't been set yet.
        //
        if (opt_label == NULL)
            opt_label = FRM_LABEL(f);
    }
    else {
        // If the request was to capture a symbol and the first level wasn't
        // a frame, go ahead and fill in with something so a nested frame
        // doesn't falsely claim to label the function with the parameter.
        //
        if (opt_label == NULL)
            opt_label = Canon(SYM_NATIVE); // !!! pick something better
    }

    // We may be in a state where we aren't fetching values from the varargs
    // in our hand, but in a subfeed it is referencing.  This subfeed can
    // be NULL, the context we recursively feed from, or an array containing
    // a single element with the array and index to feed from.
    //
    // The subfeed is operated on by address because we need to NULL it when
    // done...and if we encounter a nested varlist to chain in, we set it.
    //
    subfeed_addr = SUBFEED_ADDR_OF_FEED(feed);

handle_subfeed:
    if (*subfeed_addr != NULL) {
        //
        // Because we're recursing, we could run into trouble if someone
        // tries to chain a varargs into itself, etc.
        //
        if (C_STACK_OVERFLOWING(&op)) Trap_Stack_Overflow();

        indexor = Do_Vararg_Op_Core(
            out,
            *subfeed_addr,
            param,
            arg,
            opt_label,
            op
        );

        if (indexor != END_FLAG)
            return indexor; // type was checked already via param

        // Since the subfeed is now exhausted, clear out its pointer (which
        // will be seen by all other instances of this VARARGS!) and fall
        // through to getting values from the main feed.
        //
        *subfeed_addr = NULL;
    }

    // Reading from the main feed...

    if (GET_ARR_FLAG(feed, ARRAY_FLAG_CONTEXT_VARLIST)) {
        //
        // "Ordinary" case... use the original frame implied by the VARARGS!
        // The Reb_Frame isn't a bad pointer, we checked FRAME! is stack-live.
        //
        if (IS_END(f->value))
            goto return_end_flag;

        if (op == VARARG_OP_FIRST) {
            COPY_VALUE(out, f->value, f->specifier);
            return VA_LIST_FLAG;
        }
    }
    else {
        // We are processing an ANY-ARRAY!-based varargs, which came from
        // either a MAKE VARARGS! on an ANY-ARRAY! value -or- from a
        // MAKE ANY-ARRAY! on a varargs (which reified the varargs into an
        // array during that creation, flattening its entire output).
        //
        shared = KNOWN(ARR_HEAD(feed)); // 1 element, array or end marker

        if (IS_END(shared))
            goto return_end_flag; // exhausted

        assert(IS_BLOCK(shared)); // holds index and data values (specified)

        // A proxy call frame is created to feed from the shared array, and
        // its index will be updated (or set to END when exhausted)

        if (VAL_INDEX(shared) >= ARR_LEN(VAL_ARRAY(shared))) {
            SET_END(shared); // input now exhausted, mark for shared instances
            goto return_end_flag;
        }

        if (op == VARARG_OP_FIRST) {
            *out = *KNOWN(VAL_ARRAY_AT(shared)); // no relative values
            return VA_LIST_FLAG;
        }

        // Fill in just enough enformation to call the FETCH-based routines

        temp_frame.value = VAL_ARRAY_AT(shared);
        temp_frame.specifier = SPECIFIED;
        temp_frame.source.array = VAL_ARRAY(shared);
        temp_frame.index = VAL_INDEX(shared) + 1;
        temp_frame.out = out;
        temp_frame.pending = NULL;
        temp_frame.label = Canon(SYM_NATIVE); // !!! lie, shouldn't be used

        f = &temp_frame;
    }

    // The invariant here is that `c` has been prepared for fetching/doing
    // and has at least one value in it.
    //
    assert(NOT_END(f->value));
    assert(opt_label != NULL);
    assert(op != VARARG_OP_FIRST);

    if (IS_BAR(f->value))
        goto return_end_flag; // all functions, including varargs, stop at `|`

    // Based on the quoting class of the parameter, fulfill the varargs from
    // whatever information was loaded into `c` as the "feed" for values.
    //
    switch (pclass) {
    case PARAM_CLASS_NORMAL:
        if (op == VARARG_OP_TAIL_Q) return VA_LIST_FLAG;

        DO_NEXT_REFETCH_MAY_THROW(
            out,
            f,
            (f->flags & DO_FLAG_LOOKAHEAD)
                ? DO_FLAG_LOOKAHEAD
                : DO_FLAG_NO_LOOKAHEAD
        );

        if (THROWN(out))
            return THROWN_FLAG;

        if (GET_VAL_FLAG(out, VALUE_FLAG_EVALUATED))
            SET_VAL_FLAG(arg, VALUE_FLAG_EVALUATED);
        else
            CLEAR_VAL_FLAG(arg, VALUE_FLAG_EVALUATED);
        break;

    case PARAM_CLASS_HARD_QUOTE:
        if (op == VARARG_OP_TAIL_Q) return VA_LIST_FLAG;

        QUOTE_NEXT_REFETCH(out, f);
        CLEAR_VAL_FLAG(arg, VALUE_FLAG_EVALUATED);
        break;

    case PARAM_CLASS_SOFT_QUOTE:

        if (
            IS_GROUP(f->value)
            || IS_GET_WORD(f->value)
            || IS_GET_PATH(f->value) // these 3 cases evaluate
        ) {
            if (op == VARARG_OP_TAIL_Q) return VA_LIST_FLAG;

            if (EVAL_VALUE_CORE_THROWS(out, f->value, f->specifier))
                return THROWN_FLAG;

            if (GET_VAL_FLAG(out, VALUE_FLAG_EVALUATED))
                SET_VAL_FLAG(arg, VALUE_FLAG_EVALUATED);
            else
                CLEAR_VAL_FLAG(arg, VALUE_FLAG_EVALUATED);

            FETCH_NEXT_ONLY_MAYBE_END(f);
        }
        else { // not a soft-"exception" case, quote ordinarily
            if (op == VARARG_OP_TAIL_Q) return VA_LIST_FLAG;

            QUOTE_NEXT_REFETCH(out, f);

            CLEAR_VAL_FLAG(arg, VALUE_FLAG_EVALUATED);
        }
        break;

    default:
        assert(FALSE);
    }

    assert(NOT(THROWN(out))); // should have returned above

    // If the `c` we were updating was the stack local call we created just
    // for this function, then the new index status would be lost when this
    // routine ended.  Update the indexor state in the sub_value array.
    //
    if (f == &temp_frame) {
        assert(ANY_ARRAY(shared));
        if (IS_END(f->value))
            SET_END(shared); // signal no more to all varargs sharing value
        else {
            // The indexor is "prefetched", so although the temp_frame would
            // be ready to use again we're throwing it away, and need to
            // effectively "undo the prefetch" by taking it down by 1.  The
            //
            assert(f->index > 0);
            VAL_INDEX(shared) = f->index - 1; // update seen by all sharings
        }
    }

    // Now check to see if the value fetched through the varargs mechanism
    // was itself a VARARGS!.  If the argument explicitly says it takes
    // a VARARGS! type (a distinction from being marked variadic but taking
    // only integers, say)...then it will be passed normally.  But if it
    // is not marked as taking VARARGS! then it will become chained, so
    // that the next time this routine is called, this varargs is consulted.
    //
    if (IS_VARARGS(out) && !TYPE_CHECK(param, REB_VARARGS)) {
        assert(*subfeed_addr == NULL);

        if (GET_VAL_FLAG(out, VARARGS_FLAG_NO_FRAME))
            *subfeed_addr = VAL_VARARGS_ARRAY1(out);
        else {
            *subfeed_addr = CTX_VARLIST(VAL_VARARGS_FRAME_CTX(out));
            if (*subfeed_addr == feed) {
                //
                // This only catches direct recursions, soslightly more
                // friendly than a stack overflow error (as it's easy to
                // create direct recursions ATM due to dynamic binding)
                //
                fail (Error(RE_RECURSIVE_VARARGS));
            }
        }
        goto handle_subfeed;
    }

    if (!TYPE_CHECK(param, VAL_TYPE(out)))
        fail (Error_Arg_Type(opt_label, param, VAL_TYPE(out)));

    return VA_LIST_FLAG; // may be at end now, but reflect that at *next* call

return_end_flag:
    if (op != VARARG_OP_TAIL_Q) SET_TRASH_IF_DEBUG(out);
    return END_FLAG;
}


//
//  Do_Vararg_Op_May_Throw: C
//
// Wrapper over core recursive routine to start the initial feed going.
//
REBIXO Do_Vararg_Op_May_Throw(
    REBVAL *out,
    REBVAL *varargs,
    enum Reb_Vararg_Op op
) {
    assert(IS_VARARGS(varargs));

    if (GET_VAL_FLAG(varargs, VARARGS_FLAG_NO_FRAME)) {
        //
        // If MAKE VARARGS! was used, then there is no variadic "param".  When
        // handling them use the baseline of just picking element-by-element
        // like TAKE of a normal block would work.  Also, any datatype is
        // considered legal to pick out of it.
        //
        // With these choices, no errors should be reported which would
        // require a named symbol.  However, we name it `...` anyway.

        REBVAL fake_param;
        Val_Init_Typeset(&fake_param, ALL_64, Canon(SYM_ELLIPSIS)); // any type
        SET_VAL_FLAG(&fake_param, TYPESET_FLAG_VARIADIC); // pretend <...> tag
        INIT_VAL_PARAM_CLASS(&fake_param, PARAM_CLASS_HARD_QUOTE);

        REBIXO indexor = Do_Vararg_Op_Core(
            out,
            VAL_VARARGS_ARRAY1(varargs), // single-element array w/shared value
            &fake_param,
            &fake_param, // just need dummy place to write VALUE_FLAG_EVALUATED
            NULL, // should never be used, as no errors possible (?)
            op
        );

        assert(indexor == END_FLAG || indexor == VA_LIST_FLAG); // can't throw
        return indexor;
    }

    // If there's a frame, the check to ensure it is still on the stack is
    // done in the core routine (it has to be done recursively for any
    // frame-based subfeeds anyway).
    //
    return Do_Vararg_Op_Core(
        out,
        CTX_VARLIST(VAL_VARARGS_FRAME_CTX(varargs)),
        VAL_VARARGS_PARAM(varargs), // distinct from the frame->param!
        VAL_VARARGS_ARG(varargs), // may have changed since function started
        NULL, // have it fetch symbol from frame if call is active
        op
    );
}


//
//  MAKE_Varargs: C
//
void MAKE_Varargs(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
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
        REBARR *array1 = Make_Singular_Array(arg);
        MANAGE_ARRAY(array1);

        // must initialize subfeed pointer in union before reading from it
        //
        *SUBFEED_ADDR_OF_FEED(array1) = NULL;

        VAL_RESET_HEADER(out, REB_VARARGS);
        SET_VAL_FLAG(out, VARARGS_FLAG_NO_FRAME);
        out->extra.binding = array1;

        return;
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Varargs: C
//
void TO_Varargs(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    fail (Error_Invalid_Arg(arg));
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
    case SYM_PICK: {
        if (!IS_INTEGER(arg))
            fail (Error_Invalid_Arg(arg));

        if (VAL_INT32(arg) != 1)
            fail (Error(RE_VARARGS_NO_LOOK));

        indexor = Do_Vararg_Op_May_Throw(D_OUT, value, VARARG_OP_FIRST);
        assert(indexor == VA_LIST_FLAG || indexor == END_FLAG); // no throw
        if (indexor == END_FLAG)
            SET_BLANK(D_OUT); // want to be consistent with TAKE

        return R_OUT;
    }

    case SYM_TAIL_Q: {
        indexor = Do_Vararg_Op_May_Throw(NULL, value, VARARG_OP_TAIL_Q);
        assert(indexor == VA_LIST_FLAG || indexor == END_FLAG); // no throw
        return indexor == END_FLAG ? R_TRUE : R_FALSE;
    }

    case SYM_TAKE: {
        REFINE(2, part);
        PARAM(3, limit);
        REFINE(4, deep); // !!! doesn't seem to be implemented on ANY-ARRAY!
        REFINE(5, last);

        REBDSP dsp_orig = DSP;
        REBINT limit;

        if (REF(deep)) fail (Error(RE_MISC));
        if (REF(last)) fail (Error(RE_VARARGS_TAKE_LAST));

        if (!REF(part)) {
            indexor = Do_Vararg_Op_May_Throw(D_OUT, value, VARARG_OP_TAKE);
            if (indexor == THROWN_FLAG)
                return R_OUT_IS_THROWN;

            if (indexor == END_FLAG)
                SET_VOID(D_OUT); // currently allowed even without an /OPT

            return R_OUT;
        }

        if (IS_INTEGER(ARG(limit))) {
            limit = VAL_INT32(ARG(limit));
            if (limit < 0) limit = 0;
        }
        else if (!IS_BAR(ARG(limit))) {
            fail (Error_Invalid_Arg(ARG(limit)));
        }

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
        Val_Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
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
    if (GET_VAL_FLAG(a, VARARGS_FLAG_NO_FRAME)) {
        if (!GET_VAL_FLAG(b, VARARGS_FLAG_NO_FRAME)) return 1;
        return VAL_VARARGS_ARRAY1(a) == VAL_VARARGS_ARRAY1(b) ? 1 : 0;
    }
    else {
        if (GET_VAL_FLAG(b, VARARGS_FLAG_NO_FRAME)) return 1;
        return VAL_VARARGS_FRAME_CTX(a) == VAL_VARARGS_FRAME_CTX(b) ? 1 : 0;
    }
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
void Mold_Varargs(const REBVAL *value, REB_MOLD *mold) {
    struct Reb_Frame *f;

    REBARR *subfeed;

    assert(IS_VARARGS(value));

    Pre_Mold(value, mold);  // #[varargs! or make varargs!

    Append_Codepoint_Raw(mold->series, '[');

    if (GET_VAL_FLAG(value, VARARGS_FLAG_NO_FRAME)) {
        Append_Unencoded(mold->series, "<= ");

        { // Just [...] for now
            Append_Unencoded(mold->series, "[...]");
            goto skip_complex_mold_for_now;
        }

        subfeed = *SUBFEED_ADDR_OF_FEED(VAL_VARARGS_ARRAY1(value));
        if (subfeed != NULL)
            Append_Unencoded(mold->series, "<= (subfeed) <= "); // !!! say more

        if (IS_END(ARR_HEAD(VAL_VARARGS_ARRAY1(value))))
            Append_Unencoded(mold->series, "*exhausted*");
        else
            Mold_Value(mold, ARR_HEAD(VAL_VARARGS_ARRAY1(value)), TRUE);
    }
    else {
        const RELVAL *varargs_param = VAL_VARARGS_PARAM(value);

        REBVAL param_word;

        REBARR *varlist = VAL_BINDING(value);
        if (NOT(IS_ARRAY_MANAGED(varlist))) {
            //
            // This can happen if you internally try and PROBE() a varargs
            // item that is residing in the argument slots for a function,
            // while that function is still fulfilling its arguments.
            //
            Append_Unencoded(mold->series, "** varargs frame not fulfilled");
        }
        else if (
            GET_CTX_FLAG(VAL_VARARGS_FRAME_CTX(value), CONTEXT_FLAG_STACK) &&
            !GET_CTX_FLAG(VAL_VARARGS_FRAME_CTX(value), SERIES_FLAG_ACCESSIBLE)
        ) {
            Append_Unencoded(mold->series, "**unavailable: call ended **");
        }
        else {
            // The Reb_Frame is not a bad pointer since FRAME! is stack-live
            //
            enum Reb_Param_Class pclass = VAL_PARAM_CLASS(varargs_param);
            enum Reb_Kind kind;
            switch (pclass) {
                case PARAM_CLASS_NORMAL:
                    kind = REB_WORD;
                    break;
                case PARAM_CLASS_HARD_QUOTE:
                    kind = REB_GET_WORD;
                    break;
                case PARAM_CLASS_SOFT_QUOTE:
                    kind = REB_LIT_WORD;
                    break;
                default:
                    assert(FALSE);
            };

            // Note varargs_param is distinct from f->param!
            REBVAL param_word;
            Val_Init_Word(&param_word, kind, VAL_PARAM_CASED(varargs_param));

            Mold_Value(mold, &param_word, TRUE);

            Append_Unencoded(mold->series, " <= ");

            {// Just [...] for now
                Append_Unencoded(mold->series, "[...]");
                goto skip_complex_mold_for_now;
            }

            subfeed = *SUBFEED_ADDR_OF_FEED(
                CTX_VARLIST(VAL_VARARGS_FRAME_CTX(value)));

            if (subfeed != NULL)
                Append_Unencoded(mold->series, "<= (subfeed) <= "); // !!!

            f = CTX_FRAME(VAL_VARARGS_FRAME_CTX(value));

            if (IS_END(f->value))
                Append_Unencoded(mold->series, "*exhausted*");
            else {
                Mold_Value(mold, f->value, TRUE);

                if (f->flags & DO_FLAG_VA_LIST)
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
