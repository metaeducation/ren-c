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

static REBREQ *Req_SIO;


/***********************************************************************
**
**  Lower Level Print Interface
**
***********************************************************************/

//
//  Startup_StdIO: C
//
void Startup_StdIO(void)
{
    Req_SIO = OS_MAKE_DEVREQ(RDI_STDIO);

    // !!! "The device is already open, so this call will just setup the
    // request fields properly.
    OS_DO_DEVICE(Req_SIO, RDC_OPEN);
}


//
//  Shutdown_StdIO: C
//
void Shutdown_StdIO(void)
{
    // !!! There is no OS_FREE_DEVREQ.  Should there be?  Should this
    // include an OS_ABORT_DEVICE?
    OS_FREE(Req_SIO);
}


//
//  Print_OS_Line: C
//
// Print a new line.
//
void Print_OS_Line(void)
{
    // !!! Don't put const literal directly into mutable Req_SIO->data

    static REBYTE newline[] = "\n";

    Req_SIO->common.data = newline;
    Req_SIO->length = 1;
    Req_SIO->actual = 0;

    OS_DO_DEVICE(Req_SIO, RDC_WRITE);
}


//
//  Prin_OS_String: C
//
// Print a string (with no line terminator).
//
// The encoding options are OPT_ENC_XXX flags OR'd together.
//
void Prin_OS_String(const REBYTE *utf8, REBSIZ size, REBFLGS opts)
{
    Req_SIO->flags |= RRF_FLUSH;
    if (opts & OPT_ENC_RAW)
        Req_SIO->modes &= ~RFM_TEXT;
    else
        Req_SIO->modes |= RFM_TEXT;

    Req_SIO->actual = 0;

    DECLARE_LOCAL (result);
    SET_END(result);

    // !!! The historical division of labor between the "core" and the "host"
    // is that the host doesn't know how to poll for cancellation.  So data
    // gets broken up into small batches and it's this loop that has access
    // to the core "Do_Signals_Throws" query.  Hence one can send a giant
    // string to the OS_DO_DEVICE with RDC_WRITE and be able to interrupt it,
    // even though that device request could block forever in theory.
    //
    // There may well be a better way to go about this.
    //
    Req_SIO->common.data = m_cast(REBYTE*, utf8); // !!! promises to not write
    while (size > 0) {
        if (Do_Signals_Throws(result))
            fail (Error_No_Catch_For_Throw(result));

        assert(IS_END(result));

        // !!! Req_SIO->length is actually the "size", e.g. number of bytes.
        //
        if (size <= 1024)
            Req_SIO->length = size;
        else if (NOT(opts & OPT_ENC_RAW))
            Req_SIO->length = 1024;
        else {
            // Correct for UTF-8 batching so we don't span an encoded
            // character, back off until we hit a valid leading character.
            // Start by scanning 4 bytes back since that's the longest valid
            // UTF-8 encoded character.
            //
            Req_SIO->length = 1020;
            while ((Req_SIO->common.data[Req_SIO->length] & 0xC0) == 0x80)
                ++Req_SIO->length;
            assert(Req_SIO->length <= 1024);
        }

        OS_DO_DEVICE(Req_SIO, RDC_WRITE);

        Req_SIO->common.data += Req_SIO->length;
        size -= Req_SIO->length;
    }
}


//
//  Debug_String: C
//
void Debug_String(const REBYTE *utf8, REBSIZ size)
{
    REBOOL disabled = GC_Disabled;
    GC_Disabled = TRUE;

    Prin_OS_String(utf8, size, OPT_ENC_0);
    Print_OS_Line();

    assert(GC_Disabled == TRUE);
    GC_Disabled = disabled;
}


//
//  Debug_Line: C
//
void Debug_Line(void)
{
    Debug_String(cb_cast("\n"), 1);
}


//
//  Debug_Chars: C
//
// Print a character out a number of times.
//
void Debug_Chars(REBYTE chr, REBCNT num)
{
    assert(num < 100);

    REBYTE buffer[100];
    REBCNT i;
    for (i = 0; i < num; ++i)
        buffer[i] = chr;
    buffer[num] = '\0';
    Debug_String(buffer, num);
}


//
//  Debug_Space: C
//
// Print a number of spaces.
//
void Debug_Space(REBCNT num)
{
    if (num > 0) Debug_Chars(' ', num);
}


//
//  Debug_Values: C
//
void Debug_Values(const RELVAL *value, REBCNT count, REBCNT limit)
{
    REBCNT i1;
    REBCNT i2;
    REBUNI uc, pc = ' ';
    REBCNT n;

    for (n = 0; n < count; n++, value++) {
        Debug_Space(1);
        if (n > 0 && VAL_TYPE(value) <= REB_BLANK) Debug_Chars('.', 1);
        else {
            DECLARE_MOLD (mo);
            if (limit != 0) {
                SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
                mo->limit = limit;
            }
            Push_Mold(mo);

            Mold_Value(mo, value);
            Throttle_Mold(mo); // not using Pop_Mold(), must do explicitly

            for (i1 = i2 = mo->start; i1 < SER_LEN(mo->series); i1++) {
                uc = GET_ANY_CHAR(mo->series, i1);
                if (uc < ' ') uc = ' ';
                if (uc > ' ' || pc > ' ')
                    SET_ANY_CHAR(mo->series, i2++, uc);
                pc = uc;
            }
            SET_ANY_CHAR(mo->series, i2, '\0');

            Debug_String(BIN_AT(mo->series, mo->start), i2 - mo->start);

            Drop_Mold(mo);
        }
    }
    Debug_Line();
}


//
//  Debug_Buf: C
//
// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// Lower level formatted print for debugging purposes.
//
// 1. Does not support UNICODE.
// 2. Does not auto-expand the output buffer.
// 3. No termination buffering (limited length).
//
// Print using a format string and variable number
// of arguments.  All args must be long word aligned
// (no short or char sized values unless recast to long).
//
// Output will be held in series print buffer and
// will not exceed its max size.  No line termination
// is supplied after the print.
//
void Debug_Buf(const char *fmt, va_list *vaptr)
{
    REBOOL disabled = GC_Disabled;
    GC_Disabled = TRUE;

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Form_Args_Core(mo, fmt, vaptr);

    Debug_String(
        BIN_AT(mo->series, mo->start), SER_LEN(mo->series) - mo->start
    );

    Drop_Mold(mo);

    Debug_Line();

    assert(GC_Disabled == TRUE);
    GC_Disabled = disabled;
}


//
//  Debug_Fmt_: C
//
// Print using a format string and variable number
// of arguments.  All args must be long word aligned
// (no short or char sized values unless recast to long).
// Output will be held in series print buffer and
// will not exceed its max size.  No line termination
// is supplied after the print.
//
void Debug_Fmt_(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    Debug_Buf(fmt, &va);
    va_end(va);
}


//
//  Debug_Fmt: C
//
// Print using a formatted string and variable number
// of arguments.  All args must be long word aligned
// (no short or char sized values unless recast to long).
// Output will be held in a series print buffer and
// will not exceed its max size.  A line termination
// is supplied after the print.
//
void Debug_Fmt(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Debug_Buf(fmt, &args);
    Debug_Line();
    va_end(args);
}


//
//  Form_Hex_Pad: C
//
// Form an integer hex string in the given buffer with a
// width padded out with zeros.
// If len = 0 and val = 0, a null string is formed.
// Does not insert a #.
// Make sure you have room in your buffer before calling this!
//
REBYTE *Form_Hex_Pad(REBYTE *buf, REBI64 val, REBINT len)
{
    REBYTE buffer[MAX_HEX_LEN + 4];
    REBYTE *bp = buffer + MAX_HEX_LEN + 1;

    // !!! val parameter was REBU64 at one point; changed to REBI64
    // as this does signed comparisons (val < 0 was never true...)

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

    bp++;
    while ((*buf++ = *bp++) != '\0')
        NOOP;
    return buf - 1;
}


//
//  Form_Hex2_UTF8: C
//
// Convert byte-sized int to xx format. Very fast.
//
REBYTE *Form_Hex2_UTF8(REBYTE *bp, REBCNT val)
{
    bp[0] = Hex_Digits[(val & 0xf0) >> 4];
    bp[1] = Hex_Digits[val & 0xf];
    bp[2] = '\0';
    return bp + 2;
}


//
//  Form_Hex2_Uni: C
//
// Convert byte-sized int to xx format. Very fast.
//
REBUNI *Form_Hex2_Uni(REBUNI *up, REBCNT val)
{
    up[0] = Hex_Digits[(val & 0xf0) >> 4];
    up[1] = Hex_Digits[val & 0xf];
    up[2] = '\0';
    return up + 2;
}


//
//  Form_Hex_Esc: C
//
// Convert byte to %xx format
//
REBYTE *Form_Hex_Esc(REBYTE *bp, REBYTE b)
{
    bp[0] = '%';
    bp[1] = Hex_Digits[(b & 0xf0) >> 4];
    bp[2] = Hex_Digits[b & 0xf];
    bp[3] = '\0';
    return bp + 3;
}


//
//  Form_RGB_Uni: C
//
// Convert 24 bit RGB to xxxxxx format.
//
REBYTE *Form_RGB_Utf8(REBYTE *bp, REBCNT val)
{
#ifdef ENDIAN_LITTLE
    bp[0] = Hex_Digits[(val >>  4) & 0xf];
    bp[1] = Hex_Digits[val & 0xf];
    bp[2] = Hex_Digits[(val >> 12) & 0xf];
    bp[3] = Hex_Digits[(val >>  8) & 0xf];
    bp[4] = Hex_Digits[(val >> 20) & 0xf];
    bp[5] = Hex_Digits[(val >> 16) & 0xf];
#else
    bp[0] = Hex_Digits[(val >>  28) & 0xf];
    bp[1] = Hex_Digits[(val >> 24) & 0xf];
    bp[2] = Hex_Digits[(val >> 20) & 0xf];
    bp[3] = Hex_Digits[(val >> 16) & 0xf];
    bp[4] = Hex_Digits[(val >> 12) & 0xf];
    bp[5] = Hex_Digits[(val >>  8) & 0xf];
#endif
    bp[6] = 0;

    return bp + 6;
}


//
//  Form_Args_Core: C
//
// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// This is an internal routine used for debugging, which is something like
// `printf` (it understands %d, %s, %c) but stripped down in features.
// It also knows how to show REBVAL* values FORMed (%v) or MOLDed (%r),
// as well as REBSER* or REBARR* series molded (%m).
//
// Initially it was considered to be for low-level debug output only.  It
// was strictly ASCII, and it only supported a fixed-size output destination
// buffer.  The buffer that it used was reused by other routines, and
// nested calls would erase the content.  The choice was made to use the
// implementation techniques of MOLD and the "mold stack"...allowing nested
// calls and unicode support.  It simplified the code, at the cost of
// becoming slightly more "bootstrapped".
//
void Form_Args_Core(REB_MOLD *mo, const char *fmt, va_list *vaptr)
{
    REBYTE *cp;
    REBINT pad;
    REBYTE desc;
    REBYTE padding;
    REBSER *ser = mo->series;
    REBYTE buf[MAX_SCAN_DECIMAL];

    DECLARE_LOCAL (value);

    // buffer used for making byte-oriented renderings to add to the REBUNI
    // mold series.  Should be more formally checked as it's used for
    // integers, hex, eventually perhaps other things.
    //
    assert(MAX_SCAN_DECIMAL >= MAX_HEX_LEN);

    for (; *fmt != '\0'; fmt++) {

        // Copy format string until next % escape
        //
        while ((*fmt != '\0') && (*fmt != '%'))
            Append_Utf8_Codepoint(ser, *fmt++);

        if (*fmt != '%') break;

        pad = 1;
        padding = ' ';
        fmt++; // skip %

pick:
        switch (desc = *fmt) {

        case '0':
            padding = '0';
            // falls through
        case '-':
        case '1':   case '2':   case '3':   case '4':
        case '5':   case '6':   case '7':   case '8':   case '9':
            fmt = cs_cast(Grab_Int(cb_cast(fmt), &pad));
            goto pick;

        case 'D':
            assert(FALSE); // !!! was identical code to "d"...why "D"?
        case 'd':
            // All va_arg integer arguments will be coerced to platform 'int'
            cp = Form_Int_Pad(
                buf,
                cast(REBI64, va_arg(*vaptr, int)),
                MAX_SCAN_DECIMAL,
                pad,
                padding
            );
            Append_Unencoded_Len(ser, s_cast(buf), cast(REBCNT, cp - buf));
            break;

        case 's':
            cp = va_arg(*vaptr, REBYTE *);
            if (pad == 1) pad = LEN_BYTES(cp);
            if (pad < 0) {
                pad = -pad;
                pad -= LEN_BYTES(cp);
                for (; pad > 0; pad--)
                    Append_Utf8_Codepoint(ser, ' ');
            }
            Append_Unencoded(ser, s_cast(cp));

            // !!! see R3-Alpha for original pad logic, this is an attempt
            // to make the output somewhat match without worrying heavily
            // about the padding features of this debug routine.
            //
            pad -= LEN_BYTES(cp);

            for (; pad > 0; pad--)
                Append_Utf8_Codepoint(ser, ' ');
            break;

        case 'r':   // use Mold
        case 'v':   // use Form
            Mold_Or_Form_Value(
                mo,
                va_arg(*vaptr, const REBVAL*),
                LOGICAL(desc == 'v')
            );

            // !!! This used to "filter out ctrl chars", which isn't a bad
            // idea as a mold option (MOLD_FLAG_FILTER_CTRL) but it involves
            // some doing, as molding doesn't have a real "moment" that
            // it can always filter...since sometimes the buffer is examined
            // directly by clients vs. getting handed back.
            //
            /* for (; l > 0; l--, bp++) if (*bp < ' ') *bp = ' '; */
            break;

        case 'm': { // Mold a series
            // Init_Block would Ensure_Series_Managed, we use a raw
            // VAL_SET instead.
            //
            // !!! Better approach?  Can the series be passed directly?
            //
            REBSER* temp = va_arg(*vaptr, REBSER*);
            if (GET_SER_FLAG(temp, SERIES_FLAG_ARRAY)) {
                VAL_RESET_HEADER(value, REB_BLOCK);
                INIT_VAL_ARRAY(value, ARR(temp));
            }
            else {
                VAL_RESET_HEADER(value, REB_STRING);
                INIT_VAL_SERIES(value, temp);
            }
            VAL_INDEX(value) = 0;
            Mold_Value(mo, value);
            break;
        }

        case 'c':
            Append_Utf8_Codepoint(
                ser,
                cast(REBYTE, va_arg(*vaptr, REBINT))
            );
            break;

        case 'x':
            Append_Utf8_Codepoint(ser, '#');
            if (pad == 1) pad = 8;
            cp = Form_Hex_Pad(
                buf,
                cast(REBU64, cast(REBUPT, va_arg(*vaptr, REBYTE*))),
                pad
            );
            Append_Unencoded_Len(ser, s_cast(buf), cp - buf);
            break;

        default:
            Append_Utf8_Codepoint(ser, *fmt);
        }
    }

    TERM_SERIES(ser);
}


//
//  Form_Args: C
//
void Form_Args(REB_MOLD *mo, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    Form_Args_Core(mo, fmt, &args);
    va_end(args);
}


//
//  Startup_Raw_Print: C
//
// Initialize print module.
//
void Startup_Raw_Print(void)
{
    Init_Binary(TASK_BYTE_BUF, Make_Binary(1000));
}
