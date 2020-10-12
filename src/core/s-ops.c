//
//  File: %s-ops.c
//  Summary: "string handling utilities"
//  Section: strings
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
//  All_Bytes_ASCII: C
//
// Returns true if byte string does not use upper code page
// (e.g. no 128-255 characters)
//
bool All_Bytes_ASCII(REBYTE *bp, REBLEN len)
{
    for (; len > 0; len--, bp++)
        if (*bp >= 0x80)
            return false;

    return true;
}


//
//  Analyze_String_For_Scan: C
//
// Locate beginning byte pointer and number of bytes to prepare a string
// into a form that can be used with a Scan_XXX routine.  Used for instance
// to MAKE DATE! from a STRING!.  Rules are:
//
//     1. it's actual content (less space, newlines) <= max len
//     2. it does not contain other values ("123 456")
//     3. it's not empty or only whitespace
//
// !!! This seems to be an addition to R3-Alpha for things like TO WORD! of
// a TEXT! to use with arbitrary whitespace (Rebol2 would just include the
// whitespace in the WORD!).  In cases such like that, it is redundant with
// work done by TRANSCODE...though it is lighter weight.  It also permits
// clipping out syntax that may not be accepted by the scanner (e.g. if
// TO DATE! permitted textual syntax that was not independently LOAD-able).
// It should be reviewed.
//
const REBYTE *Analyze_String_For_Scan(
    REBSIZ *opt_size_out,
    const REBVAL *any_string,
    REBLEN max_len  // maximum length in *codepoints*
){
    REBLEN len;
    REBCHR(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, any_string);
    if (len == 0)
        fail (Error_Past_End_Raw());

    // Skip leading whitespace
    //
    REBUNI c;
    REBUNI i;
    for (i = 0; IS_SPACE(c = CHR_CODE(up)) and (i < len); ++i, --len)
        up = NEXT_STR(up);

    if (len == 0)
        fail (Error_Past_End_Raw());

    REBCHR(const*) at_index = up;

    // Skip up to max_len non-space characters.
    //
    // !!! The R3-Alpha code would fail with Error_Invalid_Chars_Raw() if
    // there were UTF-8 characters in most calls.  Only ANY-WORD! from
    // ANY-STRING! allowed it.  Though it's not clear why it wouldn't be
    // better to delegate to the scanning routine itself to give a more
    // pointed error... allow c >= 0x80 for now.
    //
    REBLEN num_chars = 0;
    do {
        ++num_chars;
        if (num_chars > max_len)
            fail (Error_Too_Long_Raw());

        --len;
        up = NEXT_STR(up);
    } while (len > 0 and not IS_SPACE(c = CHR_CODE(up)));

    if (opt_size_out)  // give back byte size before trailing spaces
        *opt_size_out = up - at_index;

    // Rest better be just spaces
    //
    for (; len > 0; --len) {
        if (not IS_SPACE(c))
            fail (Error_Invalid_Chars_Raw());
        up = NEXT_CHR(&c, up);
    }

    return at_index;
}


//
//  Xandor_Binary: C
//
// Only valid for BINARY data.
//
REBSER *Xandor_Binary(const REBVAL *verb, REBVAL *value, REBVAL *arg)
{
    const REBYTE *p0 = VAL_BIN_AT(value);
    const REBYTE *p1 = VAL_BIN_AT(arg);

    REBLEN t0 = VAL_LEN_AT(value);
    REBLEN t1 = VAL_LEN_AT(arg);

    REBLEN mt = MIN(t0, t1); // smaller array size

    // !!! This used to say "For AND - result is size of shortest input:" but
    // the code was commented out
    /*
        if (verb == A_AND || (verb == 0 && t1 >= t0))
            t2 = mt;
        else
            t2 = MAX(t0, t1);
    */

    REBLEN t2 = MAX(t0, t1);

    REBSER *series;
    if (IS_BITSET(value)) {
        //
        // Although bitsets and binaries share some implementation here,
        // they have distinct allocation functions...and bitsets need
        // to set the REBSER.misc.negated union field (BITS_NOT) as
        // it would be illegal to read it if it were cleared via another
        // element of the union.
        //
        assert(IS_BITSET(arg));
        series = Make_Bitset(t2 * 8);
    }
    else {
        // Ordinary binary
        //
        series = Make_Binary(t2);
        TERM_SEQUENCE_LEN(series, t2);
    }

    REBYTE *p2 = BIN_HEAD(series);

    switch (VAL_WORD_SYM(verb)) {
    case SYM_INTERSECT: { // and
        REBLEN i;
        for (i = 0; i < mt; i++)
            *p2++ = *p0++ & *p1++;
        CLEAR(p2, t2 - mt);
        return series; }

    case SYM_UNION: { // or
        REBLEN i;
        for (i = 0; i < mt; i++)
            *p2++ = *p0++ | *p1++;
        break; }

    case SYM_DIFFERENCE: { // xor
        REBLEN i;
        for (i = 0; i < mt; i++)
            *p2++ = *p0++ ^ *p1++;
        break; }

    case SYM_EXCLUDE: { // !!! not a "type action", word manually in %words.r
        REBLEN i;
        for (i = 0; i < mt; i++)
            *p2++ = *p0++ & ~*p1++;
        if (t0 > t1)
            memcpy(p2, p0, t0 - t1); // residual from first only
        return series; }

    default:
        fail (Error_Cannot_Use_Raw(verb, Datatype_From_Kind(REB_BINARY)));
    }

    // Copy the residual
    //
    memcpy(p2, ((t0 > t1) ? p0 : p1), t2 - mt);
    return series;
}


//
//  Complement_Binary: C
//
// Only valid for BINARY data.
//
REBSER *Complement_Binary(REBVAL *value)
{
    const REBYTE *bp = VAL_BIN_AT(value);
    REBLEN len = VAL_LEN_AT(value);

    REBSER *bin = Make_Binary(len);
    TERM_SEQUENCE_LEN(bin, len);

    REBYTE *dp = BIN_HEAD(bin);
    for (; len > 0; len--, ++bp, ++dp)
        *dp = ~(*bp);

    return bin;
}


//
//  Trim_Tail: C
//
// Used to trim off hanging spaces during FORM and MOLD.
//
void Trim_Tail(REB_MOLD *mo, REBYTE ascii)
{
    assert(ascii < 0x80);  // more work needed for multi-byte characters

    REBLEN len = STR_LEN(mo->series);
    REBSIZ size = STR_SIZE(mo->series);

    for (; size > 0; --size, --len) {
        REBYTE b = *BIN_AT(SER(mo->series), size - 1);
        if (b != ascii)
            break;
    }

    TERM_STR_LEN_SIZE(mo->series, len, size);
}


//
//  Change_Case: C
//
// Common code for string case handling.
//
void Change_Case(
    REBVAL *out,
    REBVAL *val, // !!! Not const--uses Partial(), may change index, review
    const REBVAL *part,
    bool upper
){
    if (IS_CHAR(val)) {
        REBUNI c = VAL_CHAR(val);
        Init_Char_Unchecked(out, upper ? UP_CASE(c) : LO_CASE(c));
        return;
    }

    assert(ANY_STRING(val));

    // This is a mutating operation, and we want to return the same series at
    // the same index.  However, R3-Alpha code would use Partial() and may
    // change val's index.  Capture it before potential change, review.
    //
    Move_Value(out, val);

    REBLEN len = Part_Len_May_Modify_Index(val, part);

    // !!! This assumes that all case changes will preserve the encoding size,
    // but that's not true (some strange multibyte accented characters have
    // capital or lowercase versions that are single byte).  This may be
    // uncommon enough to have special handling (only do something weird, e.g.
    // use the mold buffer, if it happens...for the remaining portion of such
    // a string...and only if the size *expands*).  Expansions also may never
    // be possible, only contractions (is that true?)  Review when UTF-8
    // Everywhere is more mature to the point this is worth worrying about.
    //
    REBCHR(*) up = VAL_STRING_AT_ENSURE_MUTABLE(val);
    REBCHR(*) dp;
    if (upper) {
        REBLEN n;
        for (n = 0; n < len; n++) {
            dp = up;

            REBUNI c;
            up = NEXT_CHR(&c, up);
            if (c < UNICODE_CASES) {
                dp = WRITE_CHR(dp, UP_CASE(c));
                assert(dp == up); // !!! not all case changes same byte size?
            }
        }
    }
    else {
        REBLEN n;
        for (n = 0; n < len; n++) {
            dp = up;

            REBUNI c;
            up = NEXT_CHR(&c, up);
            if (c < UNICODE_CASES) {
                dp = WRITE_CHR(dp, LO_CASE(c));
                assert(dp == up); // !!! not all case changes same byte size?
            }
        }
    }
}


//
//  Split_Lines: C
//
// Given a string series, split lines on CR-LF.  Give back array of strings.
//
// Note: The definition of "line" in POSIX is a sequence of characters that
// end with a newline.  Hence, the last line of a file should have a newline
// marker, or it's not a "line")
//
// https://stackoverflow.com/a/729795
//
// This routine does not require it.
//
// !!! CR support is likely to be removed...and CR will be handled as a normal
// character, with special code needed to process it.
//
REBARR *Split_Lines(const REBVAL *str)
{
    REBDSP dsp_orig = DSP;

    REBLEN len = VAL_LEN_AT(str);
    REBLEN i = VAL_INDEX(str);
    if (i == len)
        return Make_Array(0);

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    REBCHR(const*) cp = VAL_STRING_AT(str);

    REBUNI c;
    cp = NEXT_CHR(&c, cp);

    for (; i < len; ++i, cp = NEXT_CHR(&c, cp)) {
        if (c != LF && c != CR) {
            Append_Codepoint(mo->series, c);
            continue;
        }

        Init_Text(DS_PUSH(), Pop_Molded_String(mo));
        SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);

        Push_Mold(mo);

        if (c == CR) {
            REBCHR(const*) tp = NEXT_CHR(&c, cp);
            if (c == LF) {
                ++i;
                cp = tp; // treat CR LF as LF, lone CR as LF
            }
        }
    }

    // If there's any remainder we pushed in the buffer, consider the end of
    // string to be an implicit line-break

    if (STR_SIZE(mo->series) == mo->offset)
        Drop_Mold(mo);
    else {
        Init_Text(DS_PUSH(), Pop_Molded_String(mo));
        SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
    }

    return Pop_Stack_Values_Core(dsp_orig, ARRAY_FLAG_NEWLINE_AT_TAIL);
}
