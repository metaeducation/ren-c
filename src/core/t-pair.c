//
//  File: %t-pair.c
//  Summary: "pair datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  CT_Pair: C
//
REBINT CT_Pair(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);  // !!! Should this be heeded for the decimal?

    REBDEC diff;

    if ((diff = VAL_PAIR_Y_DEC(a) - VAL_PAIR_Y_DEC(b)) == 0)
        diff = VAL_PAIR_X_DEC(a) - VAL_PAIR_X_DEC(b);
    return (diff > 0.0) ? 1 : ((diff < 0.0) ? -1 : 0);
}


//
//  MAKE_Pair: C
//
REB_R MAKE_Pair(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_PAIR);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_PAIR(arg))
        return Move_Value(out, arg);

    if (IS_TEXT(arg)) {
        //
        // -1234567890x-1234567890
        //
        REBSIZ size;
        const REBYTE *bp
            = Analyze_String_For_Scan(&size, arg, VAL_LEN_AT(arg));

        if (NULL == Scan_Pair(out, bp, size))
            goto bad_make;

        return out;
    }

    const RELVAL *x;
    const RELVAL *y;

    if (ANY_NUMBER(arg)) {
        x = arg;
        y = arg;
    }
    else if (IS_BLOCK(arg)) {
        const RELVAL *item = VAL_ARRAY_AT(arg);

        if (ANY_NUMBER(item))
            x = item;
        else
            goto bad_make;

        if (IS_END(++item))
            goto bad_make;

        if (ANY_NUMBER(item))
            y = item;
        else
            goto bad_make;

        if (not IS_END(++item))
            goto bad_make;
    }
    else
        goto bad_make;

    return Init_Pair(out, x, y);

  bad_make:;

    fail (Error_Bad_Make(REB_PAIR, arg));
}


//
//  TO_Pair: C
//
REB_R TO_Pair(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Pair(out, kind, nullptr, arg);
}


//
//  Min_Max_Pair: C
//
// Note: compares on the basis of decimal value, but preserves the DECIMAL!
// or INTEGER! state of the element it kept.  This may or may not be useful.
//
void Min_Max_Pair(REBVAL *out, const REBVAL *a, const REBVAL *b, bool maxed)
{
    const REBVAL* x;
    if (VAL_PAIR_X_DEC(a) > VAL_PAIR_X_DEC(b))
        x = maxed ? VAL_PAIR_X(a) : VAL_PAIR_X(b);
    else
        x = maxed ? VAL_PAIR_X(b) : VAL_PAIR_X(a);

    const REBVAL* y;
    if (VAL_PAIR_Y_DEC(a) > VAL_PAIR_Y_DEC(b))
        y = maxed ? VAL_PAIR_Y(a) : VAL_PAIR_Y(b);
    else
        y = maxed ? VAL_PAIR_Y(b) : VAL_PAIR_Y(a);

    Init_Pair(out, x, y);
}


//
//  PD_Pair: C
//
REB_R PD_Pair(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    REBINT n = 0;

    if (IS_WORD(picker)) {
        if (VAL_WORD_SYM(picker) == SYM_X)
            n = 1;
        else if (VAL_WORD_SYM(picker) == SYM_Y)
            n = 2;
        else
            return R_UNHANDLED;
    }
    else if (IS_INTEGER(picker)) {
        n = Int32(picker);
        if (n != 1 && n != 2)
            return R_UNHANDLED;
    }
    else
        return R_UNHANDLED;

    if (not opt_setval) {
        if (n == 1)
            Move_Value(pvs->out, VAL_PAIR_X(pvs->out));
        else
            Move_Value(pvs->out, VAL_PAIR_Y(pvs->out));
        return pvs->out;
    }

    // !!! PAIR! is now generic, so it could theoretically store any type.
    // This was done to avoid creating new numeric representations in the
    // core (e.g. 32-bit integers or lower precision floats) just so they
    // could both fit in a cell.  But while it's technically possible, no
    // rendering formats for other-valued pairs has been proposed.  So only
    // integers and decimals are accepted for now.
    //
    if (not IS_INTEGER(opt_setval) and not IS_DECIMAL(opt_setval))
        return R_UNHANDLED;

    if (n == 1)
        Move_Value(VAL_PAIR_X(pvs->out), opt_setval);
    else
        Move_Value(VAL_PAIR_Y(pvs->out), opt_setval);

    // Using R_IMMEDIATE means that although we've updated pvs->out, we'll
    // leave it to the path dispatch to figure out if that can be written back
    // to some variable from which this pair actually originated.
    //
    // !!! Technically since pairs are pairings of values in Ren-C, there is
    // a series node which can be used to update their values, but could not
    // be used to update other things (like header bits) from an originating
    // variable.
    //
    return R_IMMEDIATE;
}


//
//  MF_Pair: C
//
void MF_Pair(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    Mold_Or_Form_Value(mo, VAL_PAIR_X(v), form);

    Append_Codepoint(mo->series, 'x');

    Mold_Or_Form_Value(mo, VAL_PAIR_Y(v), form);
}


//
//  REBTYPE: C
//
// !!! R3-Alpha turned all the PAIR! operations from integer to decimal, but
// they had floating point precision (otherwise you couldn't fit a full cell
// for two values into a single cell).  This meant they were neither INTEGER!
// nor DECIMAL!.  Ren-C stepped away from this idea of introducing a new
// numeric type and instead created a more compact "pairing" that could fit
// in a single series node and hold two arbitrary values.
//
// With the exception of operations that are specifically pair-aware (e.g.
// REVERSE swapping X and Y), this chains to retrigger the action onto the
// pair elements and then return a pair made of that.  This makes PAIR! have
// whatever promotion of integers to decimals the rest of the language has.
//
REBTYPE(Pair)
{
    REBVAL *v = D_ARG(1);

    REBVAL *x1 = VAL_PAIR_X(v);
    REBVAL *y1 = VAL_PAIR_Y(v);

    REBVAL *x2 = nullptr;
    REBVAL *y2 = nullptr;

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REVERSE:
        return Init_Pair(D_OUT, VAL_PAIR_Y(v), VAL_PAIR_X(v));

      case SYM_ADD:
      case SYM_SUBTRACT:
      case SYM_DIVIDE:
      case SYM_MULTIPLY:
        if (IS_PAIR(D_ARG(2))) {
            x2 = VAL_PAIR_X(D_ARG(2));
            y2 = VAL_PAIR_Y(D_ARG(2));
        }
        break;  // delegate to pairwise operation

      default:
        break;
    }

    // !!! The only way we can generically guarantee the ability to retrigger
    // an action multiple times without it ruining its arguments is to copy
    // the FRAME!.  Technically we don't need two copies, we could reuse
    // this frame...but then the retriggering would have to be done with a
    // mechanical trick vs. the standard DO, because the frame thinks it is
    // already running...and the check for that would be subverted.

    REBVAL *frame = Init_Frame(D_OUT, Context_For_Frame_May_Manage(frame_));

    Move_Value(D_ARG(1), x1);
    if (x2)
        Move_Value(D_ARG(2), x2);  // use extracted arg x instead of pair arg
    REBVAL *x_frame = rebValueQ("copy", frame, rebEND);

    Move_Value(D_ARG(1), y1);
    if (y2)
        Move_Value(D_ARG(2), y2);  // use extracted arg y instead of pair arg
    REBVAL *y_frame = rebValueQ("copy", frame, rebEND);

    return rebValue(
        "make pair! reduce [",
            "do", rebR(x_frame),
            "do", rebR(y_frame),
        "]",
    rebEND);
}
