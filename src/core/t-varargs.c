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
// The VARARGS! data type implements an abstraction layer over a call frame
// or arbitrary array of values.  All copied instances of a REB_VARARGS value
// remain in sync as values are TAKE-d out of them.  Once they report
// reaching a TAIL? they will always report TAIL?...until the call that
// spawned them is off the stack, at which point they will report an error.
//

#include "sys-core.h"


inline static void Init_For_Vararg_End(REBVAL *out, enum Reb_Vararg_Op op) {
    if (op == VARARG_OP_TAIL_Q)
        Init_True(out);
    else
        SET_END(out);
}


// Some VARARGS! are generated from a block with no frame, while others
// have a frame.  It would be inefficient to force the creation of a frame on
// each call for a BLOCK!-based varargs.  So rather than doing so, there's a
// prelude which sees if it can answer the current query just from looking one
// unit ahead.
//
inline static bool Vararg_Op_If_No_Advance_Handled(
    REBVAL *out,
    enum Reb_Vararg_Op op,
    const RELVAL *opt_look, // the first value in the varargs input
    REBSPC *specifier,
    Reb_Param_Class pclass
){
    if (IS_END(opt_look)) {
        Init_For_Vararg_End(out, op); // exhausted
        return true;
    }

    if (pclass == REB_P_NORMAL and IS_COMMA(opt_look)) {
        Init_For_Vararg_End(out, op);  // non-quoted COMMA!
        return true;
    }

    if (pclass == REB_P_NORMAL and IS_WORD(opt_look)) {
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
        // and the rules apply.  Note the raw check is faster, no need to
        // separately test for IS_END()

        const REBVAL *child_gotten = try_unwrap(
            Lookup_Word(opt_look, specifier)
        );

        if (child_gotten and VAL_TYPE(child_gotten) == REB_ACTION) {
            if (GET_ACTION_FLAG(VAL_ACTION(child_gotten), ENFIXED)) {
                if (
                    pclass == REB_P_NORMAL or
                    GET_ACTION_FLAG(VAL_ACTION(child_gotten), DEFERS_LOOKBACK)
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
        if (pclass != REB_P_HARD_QUOTE)
            fail (Error_Varargs_No_Look_Raw()); // hard quote only

        Derelativize(out, opt_look, specifier);
        SET_CELL_FLAG(out, UNEVALUATED);

        return true; // only a lookahead, no need to advance
    }

    return false; // must advance, may need to create a frame to do so
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
// If op is VARARG_OP_TAIL_Q, then it will return TRUE_VALUE or FALSE_VALUE,
// and this case cannot return a thrown value.
//
// For other ops, it will return END_NODE if at the end of variadic input,
// or D_OUT if there is a value.
//
// If an evaluation is involved, then a thrown value is possibly returned.
//
bool Do_Vararg_Op_Maybe_End_Throws_Core(
    REBVAL *out,
    enum Reb_Vararg_Op op,
    const RELVAL *vararg,
    enum Reb_Kind pclass // use REB_P_DETECT to use what's in the vararg
){
    TRASH_CELL_IF_DEBUG(out);

    const RELVAL *param = Param_For_Varargs_Maybe_Null(vararg);
    if (pclass == REB_P_DETECT)
        pclass = VAL_PARAM_CLASS(param);

    REBVAL *arg; // for updating CELL_FLAG_UNEVALUATED

    option(REBFRM*) vararg_frame;

    REBFRM *f;
    REBVAL *shared;
    if (Is_Block_Style_Varargs(&shared, vararg)) {
        //
        // We are processing an ANY-ARRAY!-based varargs, which came from
        // either a MAKE VARARGS! on an ANY-ARRAY! value -or- from a
        // MAKE ANY-ARRAY! on a varargs (which reified the varargs into an
        // array during that creation, flattening its entire output).

        vararg_frame = nullptr;
        arg = nullptr; // no corresponding varargs argument either

        if (Vararg_Op_If_No_Advance_Handled(
            out,
            op,
            IS_END(shared) ? END_NODE : STABLE_HACK(VAL_ARRAY_AT(shared)),
            IS_END(shared) ? SPECIFIED : VAL_SPECIFIER(shared),
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
        case REB_P_NORMAL: {
            REBFLGS flags = EVAL_MASK_DEFAULT | EVAL_FLAG_FULFILLING_ARG;

            DECLARE_FRAME_AT (f_temp, shared, flags);
            Push_Frame(nullptr, f_temp);

            // Note: Eval_Step_In_Subframe_Throws() is not needed here because
            // this is a single use frame, whose state can be overwritten.
            //
            if (Eval_Step_Throws(out, f_temp)) {
                Abort_Frame(f_temp);
                return true;
            }

            if (
                IS_END(f_temp->feed->value)
                or GET_FEED_FLAG(f_temp->feed, BARRIER_HIT)
            ){
                SET_END(shared);
            }
            else {
                // The indexor is "prefetched", so though the temp_frame would
                // be ready to use again we're throwing it away, and need to
                // effectively "undo the prefetch" by taking it down by 1.
                //
                assert(FRM_INDEX(f_temp) > 0);
                VAL_INDEX_UNBOUNDED(shared) = FRM_INDEX(f_temp) - 1;
            }

            Drop_Frame(f_temp);
            break; }

        case REB_P_HARD_QUOTE:
            Derelativize(out, VAL_ARRAY_AT(shared), VAL_SPECIFIER(shared));
            SET_CELL_FLAG(out, UNEVALUATED);
            VAL_INDEX_UNBOUNDED(shared) += 1;
            break;

        case REB_P_MODAL:
            fail ("Variadic modal parameters not yet implemented");

        case REB_P_SOFT_QUOTE:
            if (IS_QUOTABLY_SOFT(VAL_ARRAY_AT(shared))) {
                if (Eval_Value_Throws(
                    out, STABLE(VAL_ARRAY_AT(shared)), VAL_SPECIFIER(shared)
                )){
                    return true;
                }
            }
            else { // not a soft-"exception" case, quote ordinarily
                Derelativize(out, VAL_ARRAY_AT(shared), VAL_SPECIFIER(shared));
                SET_CELL_FLAG(out, UNEVALUATED);
            }
            VAL_INDEX_UNBOUNDED(shared) += 1;
            break;

        default:
            fail ("Invalid variadic parameter class");
        }

        if (NOT_END(shared) && VAL_INDEX(shared) >= VAL_LEN_HEAD(shared))
            SET_END(shared); // signal end to all varargs sharing value
    }
    else if (Is_Frame_Style_Varargs_May_Fail(&f, vararg)) {
        //
        // "Ordinary" case... use the original frame implied by the VARARGS!
        // (so long as it is still live on the stack)

        // The enfixed case always synthesizes an array to hold the evaluated
        // left hand side value.  (See notes on Is_Varargs_Enfix().)
        //
        assert(not Is_Varargs_Enfix(vararg));

        vararg_frame = f;
        if (VAL_VARARGS_SIGNED_PARAM_INDEX(vararg) < 0)
            arg = FRM_ARG(f, - VAL_VARARGS_SIGNED_PARAM_INDEX(vararg));
        else
            arg = FRM_ARG(f, VAL_VARARGS_SIGNED_PARAM_INDEX(vararg));

        bool hit_barrier = GET_FEED_FLAG(f->feed, BARRIER_HIT)
            and (pclass != REB_P_SOFT_QUOTE) and (pclass != REB_P_HARD_QUOTE);

        if (Vararg_Op_If_No_Advance_Handled(
            out,
            op,
            hit_barrier
                ? END_NODE
                : cast(const RELVAL *, f->feed->value), // might be END
            f_specifier,
            pclass
        )){
            goto type_check_and_return;
        }

        // Note that evaluative cases here need Eval_Step_In_Subframe_Throws(),
        // because a function is running and the frame state can't be
        // overwritten by an arbitrary evaluation.
        //
        switch (pclass) {
        case REB_P_NORMAL: {
            REBFLGS flags = EVAL_MASK_DEFAULT | EVAL_FLAG_FULFILLING_ARG;
            if (Eval_Step_In_Subframe_Throws(out, f, flags))
                return true;
            break; }

        case REB_P_HARD_QUOTE:
            Literal_Next_In_Frame(out, f);
            break;

        case REB_P_SOFT_QUOTE:
            if (IS_QUOTABLY_SOFT(f_value)) {
                if (Eval_Value_Throws(
                    SET_END(out),
                    f_value,
                    f_specifier
                )){
                    return true;
                }
                Fetch_Next_Forget_Lookback(f);
            }
            else // not a soft-"exception" case, quote ordinarily
                Literal_Next_In_Frame(out, f);
            break;

        default:
            fail ("Invalid variadic parameter class");
        }
    }
    else
        panic ("Malformed VARARG cell");

  type_check_and_return:;

    if (IS_END(out))
        return false;

    if (op == VARARG_OP_TAIL_Q) {
        assert(IS_LOGIC(out));
        return false;
    }

    if (param and not TYPE_CHECK(param, VAL_TYPE(out))) {
        //
        // !!! Array-based varargs only store the parameter list they are
        // stamped with, not the frame.  This is because storing non-reified
        // types in payloads is unsafe...only safe to store REBFRM* in a
        // binding.  So that means only one frame can be pointed to per
        // vararg.  Revisit the question of how to give better errors.
        //
        if (not vararg_frame)
            fail (out);

        fail (Error_Arg_Type(unwrap(vararg_frame), param, VAL_TYPE(out)));
    }

    if (arg) {
        if (GET_CELL_FLAG(out, UNEVALUATED))
            SET_CELL_FLAG(arg, UNEVALUATED);
        else
            CLEAR_CELL_FLAG(arg, UNEVALUATED);
    }

    // Note: may be at end now, but reflect that at *next* call

    return false; // not thrown
}


//
//  MAKE_Varargs: C
//
REB_R MAKE_Varargs(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_VARARGS);
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

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
        REBARR *array1 = Alloc_Singular(NODE_FLAG_MANAGED);
        if (IS_END(VAL_ARRAY_AT(arg)))
            SET_END(STABLE(ARR_SINGLE(array1)));
        else
            Move_Value(ARR_SINGLE(array1), arg);

        RESET_CELL(out, REB_VARARGS, CELL_MASK_VARARGS);
        VAL_VARARGS_PHASE_NODE(out) = nullptr;
        UNUSED(VAL_VARARGS_SIGNED_PARAM_INDEX(out));  // trashes in C++11
        INIT_BINDING(out, array1);

        return out;
    }

    // !!! Permit FRAME! ?

    fail (Error_Bad_Make(REB_VARARGS, arg));
}


//
//  TO_Varargs: C
//
REB_R TO_Varargs(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
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
REB_R PD_Varargs(
    REBPVS *pvs,
    const RELVAL *picker,
    option(const REBVAL*) setval
){
    UNUSED(setval);

    if (not IS_INTEGER(picker))
        fail (rebUnrelativize(picker));

    if (VAL_INT32(picker) != 1)
        fail (Error_Varargs_No_Look_Raw());

    DECLARE_LOCAL (location);
    Move_Value(location, pvs->out);

    if (Do_Vararg_Op_Maybe_End_Throws(
        pvs->out,
        VARARG_OP_FIRST,
        location
    )){
        assert(false); // VARARG_OP_FIRST can't throw
        return R_THROWN;
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
    REBVAL *value = D_ARG(1);

    switch (VAL_WORD_SYM(verb)) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // already have `value`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_TAIL_Q: {
            if (Do_Vararg_Op_Maybe_End_Throws(
                D_OUT,
                VARARG_OP_TAIL_Q,
                value
            )){
                assert(false);
                return R_THROWN;
            }
            assert(IS_LOGIC(D_OUT));
            return D_OUT; }

        default:
            break;
        }

        break; }

    case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        UNUSED(PAR(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(last))
            fail (Error_Varargs_Take_Last_Raw());

        if (not REF(part)) {
            if (Do_Vararg_Op_Maybe_End_Throws(
                D_OUT,
                VARARG_OP_TAKE,
                value
            )){
                return R_THROWN;
            }
            if (IS_END(D_OUT))
                return Init_Endish_Nulled(D_OUT);
            return D_OUT;
        }

        REBDSP dsp_orig = DSP;

        if (not IS_INTEGER(ARG(part)))
            fail (PAR(part));

        REBINT limit = VAL_INT32(ARG(part));
        if (limit < 0)
            limit = 0;

        while (limit-- > 0) {
            if (Do_Vararg_Op_Maybe_End_Throws(
                D_OUT,
                VARARG_OP_TAKE,
                value
            )){
                return R_THROWN;
            }
            if (IS_END(D_OUT))
                break;
            Move_Value(DS_PUSH(), D_OUT);
        }

        // !!! What if caller wanted a REB_GROUP, REB_PATH, or an /INTO?
        //
        return Init_Block(D_OUT, Pop_Stack_Values(dsp_orig)); }

    default:
        break;
    }

    return R_UNHANDLED;
}


//
//  CT_Varargs: C
//
// Simple comparison function stub (required for every type--rules TBD for
// levels of "exactness" in equality checking, or sort-stable comparison.)
//
REBINT CT_Varargs(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);

    // !!! For the moment, say varargs are the same if they have the same
    // source feed from which the data comes.  (This check will pass even
    // expired varargs, because the expired stub should be kept alive as
    // long as its identity is needed).
    //
    if (VAL_BINDING(a) == VAL_BINDING(b))
        return 0;
    return VAL_BINDING(a) > VAL_BINDING(b) ? 1 : -1;
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
void MF_Varargs(REB_MOLD *mo, REBCEL(const*) v, bool form) {
    UNUSED(form);

    Pre_Mold(mo, v);  // #[varargs! or make varargs!

    Append_Codepoint(mo->series, '[');

    Reb_Param_Class pclass;
    const RELVAL *param = Param_For_Varargs_Maybe_Null(v);
    if (param == NULL) {
        pclass = REB_P_HARD_QUOTE;
        Append_Ascii(mo->series, "???"); // never bound to an argument
    }
    else {
        enum Reb_Kind kind;
        bool quoted = false;
        switch ((pclass = VAL_PARAM_CLASS(param))) {
        case REB_P_NORMAL:
            kind = REB_WORD;
            break;

        case REB_P_HARD_QUOTE:
            kind = REB_WORD;
            quoted = true;
            break;

        case REB_P_SOFT_QUOTE:
            kind = REB_GET_WORD;
            break;

        default:
            panic (NULL);
        };

        DECLARE_LOCAL (param_word);
        Init_Any_Word(param_word, kind, VAL_PARAM_SPELLING(param));
        if (quoted)
            Quotify(param_word, 1);
        Mold_Value(mo, param_word);
    }

    Append_Ascii(mo->series, " => ");

    REBFRM *f;
    REBVAL *shared;
    if (Is_Block_Style_Varargs(&shared, v)) {
        if (IS_END(shared))
            Append_Ascii(mo->series, "[]");
        else if (pclass == REB_P_HARD_QUOTE)
            Mold_Value(mo, shared); // full feed can be shown if hard quoted
        else
            Append_Ascii(mo->series, "[...]"); // can't look ahead
    }
    else if (Is_Frame_Style_Varargs_Maybe_Null(&f, v)) {
        if (f == NULL)
            Append_Ascii(mo->series, "!!!");
        else if (
            IS_END(f->feed->value)
            or GET_FEED_FLAG(f->feed, BARRIER_HIT)
        ){
            Append_Ascii(mo->series, "[]");
        }
        else if (pclass == REB_P_HARD_QUOTE) {
            Append_Ascii(mo->series, "[");
            Mold_Value(mo, f->feed->value); // one value shown if hard quoted
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
//  {Returns TRUE if an ACTION! may take a variable number of arguments.}
//
//      return: [logic!]
//      action [action!]
//  ]
//
REBNATIVE(variadic_q)
{
    INCLUDE_PARAMS_OF_VARIADIC_Q;

    REBVAL *param = ACT_PARAMS_HEAD(VAL_ACTION(ARG(action)));
    for (; NOT_END(param); ++param) {
        if (Is_Param_Variadic(param))
            return Init_True(D_OUT);
    }

    return Init_False(D_OUT);
}
