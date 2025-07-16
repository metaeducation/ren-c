//
//  file: %s-ops.c
//  summary: "string handling utilities"
//  section: strings
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
bool All_Bytes_ASCII(Byte *bp, REBLEN len)
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
// !!! Strings are in transition to becoming "UTF-8 Everywhere" but are not
// there yet.  So this routine can't actually give back a pointer compatible
// with the scan.  Leverages Temp_UTF8_At_Managed, so the pointer that is
// returned could be GC'd if it's not guarded and evaluator logic runs.
//
Byte *Analyze_String_For_Scan(
    Size *opt_size_out,
    const Value* any_string,
    REBLEN max_len // maximum length in *codepoints*
){
    Ucs2(const*) up = String_At(any_string);
    REBLEN index = VAL_INDEX(any_string);
    REBLEN len = Series_Len_At(any_string);
    if (len == 0)
        panic (Error_Past_End_Raw());

    Ucs2Unit c;

    // Skip leading whitespace
    //
    for (; index < len; ++index, --len) {
        up = Ucs2_Next(&c, up);
        if (not IS_SPACE(c))
            break;
    }

    // Skip up to max_len non-space characters.
    //
    REBLEN num_chars = 0;
    for (; len > 0;) {
        ++num_chars;
        --len;

        // The R3-Alpha code would fail with Error_Invalid_Chars_Raw() if
        // there were UTF-8 characters in most calls.  Only ANY-WORD! from
        // ANY-STRING! allowed it.  Though it's not clear why it wouldn't be
        // better to delegate to the scanning routine itself to give a
        // more pointed error... allow c >= 0x80 for now.

        if (num_chars > max_len)
            panic (Error_Too_Long_Raw());

        up = Ucs2_Next(&c, up);
        if (IS_SPACE(c)) {
            --len;
            break;
        }
    }

    // Rest better be just spaces
    //
    for (; len > 0; --len) {
        up = Ucs2_Next(&c, up);
        if (!IS_SPACE(c))
            panic (Error_Invalid_Chars_Raw());
    }

    if (num_chars == 0)
        panic (Error_Past_End_Raw());

    DECLARE_VALUE (reindexed);
    Copy_Cell(reindexed, any_string);
    VAL_INDEX(reindexed) = index;

    Size offset;
    Binary* temp = Temp_UTF8_At_Managed(
        &offset, opt_size_out, reindexed, Series_Len_At(reindexed)
    );

    return Binary_At(temp, offset);
}


//
//  Temp_UTF8_At_Managed: C
//
// !!! This is a routine that detected whether an R3-Alpha string was ASCII
// and hence could be reused as-is for UTF-8 purposes.  If it could not, a
// temporary string would be created for the string (which would either be
// byte-sized and have codepoints > 128, or wide characters and thus be
// UTF-8 incompatible).
//
// This branch of code requires it to always convert strings, because they
// are all encoded as UCS-2.  Modern Ren-C does not need it, because all
// strings are "UTF-8 Everywhere".  There will be no patching this branch
// to the new code--it's too complex--so this will always be "Latin1 Nowhere"
// and always involve an allocation.
//
// Mutation of the result is not allowed because those mutations will not
// be reflected in the original string, due to generation.
//
Binary* Temp_UTF8_At_Managed(
    Size *offset_out,
    Size *opt_size_out,
    const Cell* str,
    REBLEN length_limit
){
#if RUNTIME_CHECKS
    if (not Any_String(str)) {
        printf("Temp_UTF8_At_Managed() called on non-ANY-STRING!");
        crash (str);
    }
#endif

    assert(length_limit <= Series_Len_At(str));

    Binary* bin = Make_Utf8_From_String_At_Limit(str, length_limit);
    assert(BYTE_SIZE(bin));

    Manage_Flex(bin);
    Set_Flex_Info(bin, FROZEN_DEEP);

    *offset_out = 0;
    if (opt_size_out != nullptr)
        *opt_size_out = Binary_Len(bin);
    return bin;
}


//
//  Xandor_Binary: C
//
// Only valid for BINARY data.
//
Binary* Xandor_Binary(Value* verb, Value* value, Value* arg)
{
    Byte *p0 = Is_Binary(value)
        ? Blob_At(value)
        : Binary_Head(Cell_Bitset(value));
    Byte *p1 = Is_Binary(arg)
        ? Blob_At(arg)
        : Binary_Head(Cell_Bitset(arg));

    REBLEN t0 = Is_Binary(value)
        ? Series_Len_At(value)
        : Binary_Len(Cell_Bitset(value));
    REBLEN t1 = Is_Binary(arg)
        ? Series_Len_At(arg)
        : Binary_Len(Cell_Bitset(arg));

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

    Binary* series;
    if (Is_Bitset(value)) {
        //
        // Although bitsets and binaries share some implementation here,
        // they have distinct allocation functions...and bitsets need
        // to set the Stub.misc.negated union field (BITS_NOT) as
        // it would be illegal to read it if it were cleared via another
        // element of the union.
        //
        assert(Is_Bitset(arg));
        series = Make_Bitset(t2 * 8);
    }
    else {
        // Ordinary binary
        //
        series = Make_Binary(t2);
        Term_Non_Array_Flex_Len(series, t2);
    }

    Byte *p2 = Binary_Head(series);

    switch (Word_Id(verb)) {
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
        panic (Error_Illegal_Action(TYPE_BINARY, verb));
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
Flex* Complement_Binary(Value* value)
{
    const Byte *bp = Blob_At(value);
    REBLEN len = Series_Len_At(value);

    Binary* bin = Make_Binary(len);
    Term_Binary_Len(bin, len);

    Byte *dp = Binary_Head(bin);
    for (; len > 0; len--, ++bp, ++dp)
        *dp = ~(*bp);

    return bin;
}


//
//  Shuffle_String: C
//
// Randomize a string. Return a new string series.
// Handles both BYTE and UNICODE strings.
//
void Shuffle_String(Value* value, bool secure)
{
    REBLEN n;
    REBLEN k;
    Flex* series = Cell_Flex(value);
    REBLEN idx     = VAL_INDEX(value);
    Ucs2Unit swap;

    for (n = Series_Len_At(value); n > 1;) {
        k = idx + (REBLEN)Random_Int(secure) % n;
        n--;
        swap = GET_ANY_CHAR(series, k);
        SET_ANY_CHAR(series, k, GET_ANY_CHAR(series, n + idx));
        SET_ANY_CHAR(series, n + idx, swap);
    }
}


//
//  Trim_Tail: C
//
// Used to trim off hanging spaces during FORM and MOLD.
//
void Trim_Tail(Binary* src, Byte chr)
{
    REBLEN tail;
    for (tail = Binary_Len(src); tail > 0; tail--) {
        Ucs2Unit c = *Binary_At(src, tail - 1);
        if (c != chr)
            break;
    }
    Term_Binary_Len(src, tail);
}


//
//  Change_Case: C
//
// Common code for string case handling.
//
void Change_Case(Value* out, Value* val, Value* part, bool upper)
{
    Copy_Cell(out, val);

    if (Is_Char(val)) {
        Ucs2Unit c = VAL_CHAR(val);
        if (c < UNICODE_CASES) {
            c = upper ? UP_CASE(c) : LO_CASE(c);
        }
        VAL_CHAR(out) = c;
        return;
    }

    // String series:

    Panic_If_Read_Only_Flex(Cell_Flex(val));

    REBLEN len = Part_Len_May_Modify_Index(val, part);
    REBLEN n = 0;

    if (VAL_BYTE_SIZE(val)) {
        Byte *bp = Blob_At(val);
        if (upper)
            for (; n != len; n++)
                bp[n] = cast(Byte, UP_CASE(bp[n]));
        else {
            for (; n != len; n++)
                bp[n] = cast(Byte, LO_CASE(bp[n]));
        }
    } else {
        Ucs2Unit* up = String_At(val);
        if (upper) {
            for (; n != len; n++) {
                if (up[n] < UNICODE_CASES)
                    up[n] = UP_CASE(up[n]);
            }
        }
        else {
            for (; n != len; n++) {
                if (up[n] < UNICODE_CASES)
                    up[n] = LO_CASE(up[n]);
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
Array* Split_Lines(const Value* str)
{
    StackIndex base = TOP_INDEX;

    Strand* s = Cell_Strand(str);
    REBLEN len = Series_Len_At(str);
    REBLEN i = VAL_INDEX(str);

    Ucs2(const*) start = String_At(str);
    Ucs2(const*) up = start;

    if (i == len)
        return Make_Array(0);

    Ucs2Unit c;
    up = Ucs2_Next(&c, up);
    ++i;

    while (i != len) {
        if (c == LF or c == CR) {
            Init_Text(
                PUSH(),
                Copy_Non_Array_Flex_At_Len(
                    s,
                    start - Strand_Head(s),
                    up - start - 1
                )
            );
            Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            start = up;
            if (c == CR) {
                up = Ucs2_Next(&c, up);
                ++i;
                if (i == len)
                    break; // if it was the last CR/LF don't fetch again

                if (c != LF)
                    continue; // already did next character fetch

                start = up; // remark start, fall through and fetch again
            }
        }

        ++i;
        up = Ucs2_Next(&c, up);
    }

    // `c` is now the last character in the string.  See remarks above about
    // not requiring the last character to be a newline.

    if (c == CR or c == LF)
        up = Ucs2_Back(nullptr, up); // back up

    if (up > start) {
        Init_Text(
            PUSH(),
            Copy_Non_Array_Flex_At_Len(
                s,
                start - Strand_Head(s),
                up - start  // no -1, backed up if '\n'
            )
        );
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    }

    return Pop_Stack_Values_Core(base, ARRAY_FLAG_NEWLINE_AT_TAIL);
}
