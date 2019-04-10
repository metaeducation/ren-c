//
//  File: %t-char.c
//  Summary: "character datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// See %sys-char.h for notes.

#include "sys-core.h"


// Index into the table below with the first byte of a UTF-8 sequence to
// get the number of trailing bytes that are supposed to follow it.
// Note that *legal* UTF-8 values can't have 4 or 5-bytes. The table is
// left as-is for anyone who may want to do such conversion, which was
// allowed in earlier algorithms.
//
const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};


// Magic values subtracted from a buffer value during UTF8 conversion.
// This table contains as many values as there might be trailing bytes
// in a UTF-8 sequence.
//
const uint_fast32_t offsetsFromUTF8[6] = {
    0x00000000UL, 0x00003080UL, 0x000E2080UL,
    0x03C82080UL, 0xFA082080UL, 0x82082080UL
};


// Once the bits are split out into bytes of UTF-8, this is a mask OR-ed
// into the first byte, depending on how many bytes follow.  There are
// as many entries in this table as there are UTF-8 sequence types.
// (I.e., one byte sequence, two byte... etc.). Remember that sequencs
// for *legal* UTF-8 will be 4 or fewer bytes total.
//
const uint_fast8_t firstByteMark[7] = {
    0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC
};


//
//  CT_Char: C
//
REBINT CT_Char(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    REBINT num;

    if (mode >= 0) {
        if (mode == 0)
            num = LO_CASE(VAL_CHAR(a)) - LO_CASE(VAL_CHAR(b));
        else
            num = VAL_CHAR(a) - VAL_CHAR(b);
        return (num == 0);
    }

    num = VAL_CHAR(a) - VAL_CHAR(b);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}


//
//  MAKE_Char: C
//
REB_R MAKE_Char(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_CHAR);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    switch(VAL_TYPE(arg)) {
    case REB_CHAR:  // !!! is this really necessary for MAKE CHAR!?
        return Move_Value(out, arg);

    case REB_INTEGER:
    case REB_DECIMAL: {
        REBINT n = Int32(arg);
        return Init_Char_May_Fail(out, n); }

      case REB_BINARY: {
        const REBYTE *bp = VAL_BIN_HEAD(arg);
        REBSIZ len = VAL_LEN_AT(arg);
        if (len == 0)
            goto bad_make;

        REBUNI uni;
        if (*bp <= 0x80) {
            if (len != 1)
                goto bad_make;

            uni = *bp;
        }
        else {
            --len;
            bp = Back_Scan_UTF8_Char(&uni, bp, &len);
            if (!bp || len != 0) // must be valid UTF8 and consume all data
                goto bad_make;
        }
        return Init_Char_May_Fail(out, uni); }

      case REB_TEXT:
        //
        // !!! The R3-Alpha and Red behavior or `make char! next "abc"` is
        // to give back #"b".  This is of questionable use, as it does the
        // same thing as FIRST.  More useful would be if it translated
        // escape sequence strings like "^(AEBD)" or HTML entity names.
        //
        if (VAL_INDEX(arg) >= VAL_LEN_HEAD(arg))
            goto bad_make;
        return Init_Char_Unchecked(out, CHR_CODE(VAL_STRING_AT(arg)));

      default:
        break;
    }

  bad_make:
    fail (Error_Bad_Make(REB_CHAR, arg));
}


//
//  TO_Char: C
//
REB_R TO_Char(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Char(out, kind, nullptr, arg);
}


static REBINT Math_Arg_For_Char(REBVAL *arg, const REBVAL *verb)
{
    switch (VAL_TYPE(arg)) {
    case REB_CHAR:
        return VAL_CHAR(arg);

    case REB_INTEGER:
        return VAL_INT32(arg);

    case REB_DECIMAL:
        return cast(REBINT, VAL_DECIMAL(arg));

    default:
        fail (Error_Math_Args(REB_CHAR, verb));
    }
}


//
//  MF_Char: C
//
void MF_Char(REB_MOLD *mo, const REBCEL *v, bool form)
{
    REBUNI c = VAL_CHAR(v);

    if (form)
        Append_Codepoint(mo->series, c);
    else {
        bool parened = GET_MOLD_FLAG(mo, MOLD_FLAG_ALL);

        Append_Ascii(mo->series, "#\"");
        Mold_Uni_Char(mo, c, parened);
        Append_Codepoint(mo->series, '"');
    }
}


//
//  REBTYPE: C
//
REBTYPE(Char)
{
    // Don't use a REBUNI for chr, because it does signed math and then will
    // detect overflow.
    //
    REBI64 chr = cast(REBI64, VAL_CHAR(D_ARG(1)));
    REBI64 arg;

    switch (VAL_WORD_SYM(verb)) {

    case SYM_ADD: {
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr += arg;
        break; }

    case SYM_SUBTRACT: {
        arg = Math_Arg_For_Char(D_ARG(2), verb);

        // Rebol2 and Red return CHAR! values for subtraction from another
        // CHAR! (though Red checks for overflow and errors on something like
        // `subtract #"^(00)" #"^(01)"`, vs returning #"^(FF)").
        //
        // R3-Alpha chose to return INTEGER! and gave a signed difference, so
        // the above would give -1.
        //
        if (IS_CHAR(D_ARG(2))) {
            Init_Integer(D_OUT, chr - arg);
            return D_OUT;
        }

        chr -= arg;
        break; }

    case SYM_MULTIPLY:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr *= arg;
        break;

    case SYM_DIVIDE:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        if (arg == 0)
            fail (Error_Zero_Divide_Raw());
        chr /= arg;
        break;

    case SYM_REMAINDER:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        if (arg == 0)
            fail (Error_Zero_Divide_Raw());
        chr %= arg;
        break;

    case SYM_INTERSECT:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr &= cast(REBUNI, arg);
        break;

    case SYM_UNION:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr |= cast(REBUNI, arg);
        break;

    case SYM_DIFFERENCE:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr ^= cast(REBUNI, arg);
        break;

    case SYM_COMPLEMENT:
        chr = cast(REBUNI, ~chr);
        break;

    case SYM_EVEN_Q:
        return Init_Logic(D_OUT, did (cast(REBUNI, ~chr) & 1));

    case SYM_ODD_Q:
        return Init_Logic(D_OUT, did (chr & 1));

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));
        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            Set_Random(chr);
            return nullptr;
        }
        if (chr == 0) break;
        chr = cast(REBUNI, 1 + cast(REBCNT, Random_Int(REF(secure)) % chr));
        break; }

    default:
        return R_UNHANDLED;
    }

    if (chr < 0) // DEBUG_UTF8_EVERYWHERE
        fail (Error_Type_Limit_Raw(Datatype_From_Kind(REB_CHAR)));

    return Init_Char_May_Fail(D_OUT, cast(REBUNI, chr));
}

