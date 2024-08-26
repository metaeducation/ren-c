//
//  File: %d-print.c
//  Summary: "low-level console print interface"
//  Section: debug
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
// R3 is intended to run on fairly minimal devices, so this code may
// duplicate functions found in a typical C lib. That's why output
// never uses standard clib printf functions.
//

/*
        Print_OS... - low level OS output functions
        Out_...     - general console output functions
        Debug_...   - debug mode (trace) output functions
*/

#include "sys-core.h"



/***********************************************************************
**
**  Lower Level Print Interface
**
***********************************************************************/


//
//  Form_Hex_Pad: C
//
// Form integer hex string and pad width with zeros.  Does not insert a #.
//
void Form_Hex_Pad(
    REB_MOLD *mo,
    REBI64 val, // !!! was REBU64 in R3-Alpha, but code did sign comparisons!
    REBINT len
){
    Byte buffer[MAX_HEX_LEN + 4];
    Byte* bp = buffer + MAX_HEX_LEN + 1;

    REBI64 sgn = (val < 0) ? -1 : 0;

    len = MIN(len, MAX_HEX_LEN);
    *bp-- = 0;
    while (val != sgn && len > 0) {
        *bp-- = Hex_Digits[val & 0xf];
        val >>= 4;
        len--;
    }

    for (; len > 0; len--)
        *bp-- = (sgn != 0) ? 'F' : '0';

    for (++bp; *bp != '\0'; ++bp)
        Append_Codepoint(mo->series, *bp);
}


//
//  Form_Hex2: C
//
// Convert byte-sized int to xx format.
//
void Form_Hex2(REB_MOLD *mo, Byte b)
{
    Append_Codepoint(mo->series, Hex_Digits[(b & 0xf0) >> 4]);
    Append_Codepoint(mo->series, Hex_Digits[b & 0xf]);
}


//
//  Form_Hex_Esc: C
//
// Convert byte to %xx format
//
void Form_Hex_Esc(REB_MOLD *mo, Byte b)
{
    Append_Codepoint(mo->series, '%');
    Append_Codepoint(mo->series, Hex_Digits[(b & 0xf0) >> 4]);
    Append_Codepoint(mo->series, Hex_Digits[b & 0xf]);
}


//
//  Form_RGBA: C
//
// Convert 32 bit RGBA to xxxxxx format.
//
void Form_RGBA(REB_MOLD *mo, const Byte* dp)
{
    REBLEN len_old = String_Len(mo->series);
    Size used_old = String_Size(mo->series);

    Expand_Flex_Tail(mo->series, 8);  // grow by 8 bytes, may realloc

    Byte* bp = Binary_At(mo->series, used_old);  // potentially new buffer

    bp[0] = Hex_Digits[(dp[0] >> 4) & 0xf];
    bp[1] = Hex_Digits[dp[0] & 0xf];
    bp[2] = Hex_Digits[(dp[1] >> 4) & 0xf];
    bp[3] = Hex_Digits[dp[1] & 0xf];
    bp[4] = Hex_Digits[(dp[2] >> 4) & 0xf];
    bp[5] = Hex_Digits[dp[2] & 0xf];
    bp[6] = Hex_Digits[(dp[3] >> 4) & 0xf];
    bp[7] = Hex_Digits[dp[3] & 0xf];
    bp[8] = '\0';

    Term_String_Len_Size(mo->series, len_old + 8, used_old + 8);
}


//
//  Startup_Raw_Print: C
//
// Initialize print module.
//
void Startup_Raw_Print(void)
{
    TG_Byte_Buf = Make_Binary(1000);
}


//
//  Shutdown_Raw_Print: C
//
void Shutdown_Raw_Print(void)
{
    Free_Unmanaged_Flex(TG_Byte_Buf);
    TG_Byte_Buf = NULL;
}
