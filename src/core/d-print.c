/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  d-print.c
**  Summary: low-level console print interface
**  Section: debug
**  Author:  Carl Sassenrath
**  Notes:
**    R3 is intended to run on fairly minimal devices, so this code may
**    duplicate functions found in a typical C lib. That's why output
**    never uses standard clib printf functions.
**
***********************************************************************/

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
//  Init_StdIO: C
//
void Init_StdIO(void)
{
    //OS_CALL_DEVICE(RDI_STDIO, RDC_INIT);
    Req_SIO = OS_MAKE_DEVREQ(RDI_STDIO);
    if (!Req_SIO) panic (Error(RE_IO_ERROR));

    // The device is already open, so this call will just setup
    // the request fields properly.
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

    if (Req_SIO->error) panic (Error(RE_IO_ERROR));
}


//
//  Prin_OS_String: C
// 
// Print a string (with no line terminator).
// 
// The encoding options are OPT_ENC_XXX flags OR'd together.
//
void Prin_OS_String(const void *p, REBCNT len, REBFLG opts)
{
    #define BUF_SIZE 1024
    REBYTE buffer[BUF_SIZE]; // on stack
    REBYTE *buf = &buffer[0];
    REBCNT len2;
    const REBOOL is_uni = (opts & OPT_ENC_UNISRC) != 0;

    const REBYTE *bp = is_uni ? NULL : cast(const REBYTE *, p);
    const REBUNI *up = is_uni ? cast(const REBUNI *, p) : NULL;

    if (!p) panic (Error(RE_NO_PRINT_PTR));

    // Determine length if not provided:
    if (len == UNKNOWN) len = is_uni ? Strlen_Uni(up) : LEN_BYTES(bp);

    SET_FLAG(Req_SIO->flags, RRF_FLUSH);

    Req_SIO->actual = 0;
    Req_SIO->common.data = buf;
    buffer[0] = 0; // for debug tracing

    if (opts & OPT_ENC_RAW) {
        Do_Signals();

        // Used by verbatim terminal output, e.g. print of a BINARY!
        assert(!is_uni);
        Req_SIO->length = len;

        // Mutability cast, but RDC_WRITE should not be modifying the buffer
        // (doing so could yield undefined behavior)
        Req_SIO->common.data = m_cast(REBYTE *, bp);

        OS_DO_DEVICE(Req_SIO, RDC_WRITE);
        if (Req_SIO->error) panic (Error(RE_IO_ERROR));
    }
    else {
        while ((len2 = len) > 0) {
            Do_Signals();

            Req_SIO->length = Encode_UTF8(
                buf,
                BUF_SIZE - 4,
                is_uni ? cast(const void *, up) : cast(const void *, bp),
                &len2,
                opts
            );

            if (is_uni) up += len2; else bp += len2;
            len -= len2;

            OS_DO_DEVICE(Req_SIO, RDC_WRITE);
            if (Req_SIO->error) panic (Error(RE_IO_ERROR));
        }
    }
}


//
//  Out_Value: C
//
void Out_Value(const REBVAL *value, REBCNT limit, REBOOL mold, REBINT lines)
{
    Print_Value(value, limit, mold); // higher level!
    for (; lines > 0; lines--) Print_OS_Line();
}


//
//  Out_Str: C
//
void Out_Str(const REBYTE *bp, REBINT lines)
{
    Prin_OS_String(bp, UNKNOWN, OPT_ENC_CRLF_MAYBE);
    for (; lines > 0; lines--) Print_OS_Line();
}


/***********************************************************************
**
**  Debug Print Interface
**
**      If the Trace_Buffer exists, then output goes there,
**      otherwise output goes to OS output.
**
***********************************************************************/


//
//  Enable_Backtrace: C
//
void Enable_Backtrace(REBFLG on)
{
    if (on) {
        if (Trace_Limit == 0) {
            Trace_Limit = 100000;
            Trace_Buffer = Make_Binary(Trace_Limit);
            LABEL_SERIES(Trace_Buffer, "trace-buffer");
        }
    }
    else {
        if (Trace_Limit) Free_Series(Trace_Buffer);
        Trace_Limit = 0;
        Trace_Buffer = 0;
    }
}


//
//  Display_Backtrace: C
//
void Display_Backtrace(REBCNT lines)
{
    REBCNT tail;
    REBCNT i;

    if (Trace_Limit > 0) {
        tail = Trace_Buffer->tail;
        i = tail - 1;
        for (lines++ ;lines > 0; lines--, i--) {
            i = Find_Str_Char(Trace_Buffer, 0, i, tail, -1, LF, 0);
            if (i == NOT_FOUND || i == 0) {
                i = 0;
                break;
            }
        }

        if (lines == 0) i += 2; // start of next line
        Prin_OS_String(BIN_SKIP(Trace_Buffer, i), tail - i, OPT_ENC_CRLF_MAYBE);
        //RESET_SERIES(Trace_Buffer);
    }
    else {
        Out_Str(cb_cast("backtrace not enabled"), 1);
    }
}


//
//  Debug_String: C
//
void Debug_String(const void *p, REBCNT len, REBOOL uni, REBINT lines)
{
    REBUNI uc;
    const REBYTE *bp = uni ? NULL : cast(const REBYTE *, p);
    const REBUNI *up = uni ? cast(const REBUNI *, p) : NULL;

    REBINT disabled = GC_Disabled;
    GC_Disabled = 1;

    if (Trace_Limit > 0) {
        if (Trace_Buffer->tail >= Trace_Limit)
            Remove_Series(Trace_Buffer, 0, 2000);
        if (len == UNKNOWN) len = uni ? Strlen_Uni(up) : LEN_BYTES(bp);
        // !!! account for unicode!
        for (; len > 0; len--) {
            uc = uni ? *up++ : *bp++;
            Append_Codepoint_Raw(Trace_Buffer, uc);
        }
        //Append_Unencoded_Len(Trace_Buffer, bp, len);
        for (; lines > 0; lines--) Append_Codepoint_Raw(Trace_Buffer, LF);
    }
    else {
        Prin_OS_String(
            p, len, (uni ? OPT_ENC_UNISRC : 0) | OPT_ENC_CRLF_MAYBE
        );
        for (; lines > 0; lines--) Print_OS_Line();
    }

    assert(GC_Disabled == 1);
    GC_Disabled = disabled;
}


//
//  Debug_Line: C
//
void Debug_Line(void)
{
    Debug_String(cb_cast(""), UNKNOWN, 0, 1);
}


//
//  Debug_Str: C
// 
// Print a string followed by a newline.
//
void Debug_Str(const char *str)
{
    Debug_String(cb_cast(str), UNKNOWN, 0, 1);
}


//
//  Debug_Uni: C
// 
// Print debug unicode string followed by a newline.
//
void Debug_Uni(const REBSER *ser)
{
    const REBFLG encopts = OPT_ENC_UNISRC | OPT_ENC_CRLF_MAYBE;
    REBCNT ul;
    REBCNT bl;
    REBYTE buf[1024];
    REBUNI *up = UNI_HEAD(ser);
    REBCNT size = SERIES_LEN(ser);

    REBINT disabled = GC_Disabled;
    GC_Disabled = 1;

    while (size > 0) {
        ul = size;
        bl = Encode_UTF8(buf, 1020, up, &ul, encopts);
        Debug_String(buf, bl, 0, 0);
        size -= ul;
        up += ul;
    }

    Debug_Line();

    assert(GC_Disabled == 1);
    GC_Disabled = disabled;
}


//
//  Debug_Series: C
//
void Debug_Series(REBSER *ser)
{
    REBINT disabled = GC_Disabled;
    GC_Disabled = 1;

    // Invalid series would possibly (but not necessarily) crash the print
    // routines--which are the same ones used to output a series normally.
    // Hence Debug_Series should not be used to attempt to print a known
    // malformed series.  ASSERT_SERIES will likely give a more pointed
    // message about what is wrong than just crashing the print code...

    ASSERT_SERIES(ser);

    // This routine is also a little catalog of the outlying series
    // types in terms of sizing, just to know what they are.

    if (BYTE_SIZE(ser))
        Debug_Str(s_cast(BIN_HEAD(ser)));
    else if (Is_Array_Series(ser)) {
        REBVAL value;
        // May not actually be a REB_BLOCK, but we put it in a value
        // container for now saying it is so we can output it.  It may be
        // a frame and we may not want to Manage_Series here, so we use a
        // raw VAL_SET instead of Val_Init_Block
        VAL_SET(&value, REB_BLOCK);
        VAL_SERIES(&value) = ser;
        VAL_INDEX(&value) = 0;
        Debug_Fmt("%r", &value);
    } else if (SERIES_WIDE(ser) == sizeof(REBUNI))
        Debug_Uni(ser);
    else if (ser == Bind_Table) {
        // Dump bind table somehow?
        Panic_Series(ser);
    } else if (ser == PG_Word_Table.hashes) {
        // Dump hashes somehow?
        Panic_Series(ser);
    } else if (ser == GC_Series_Guard) {
        // Dump protected series pointers somehow?
        Panic_Series(ser);
    } else if (ser == GC_Value_Guard) {
        // Dump protected value pointers somehow?
        Panic_Series(ser);
    }
    else
        Panic_Series(ser);

    assert(GC_Disabled == 1);
    GC_Disabled = disabled;
}


//
//  Debug_Num: C
// 
// Print a string followed by a number.
//
void Debug_Num(const REBYTE *str, REBINT num)
{
    REBYTE buf[40];

    Debug_String(str, UNKNOWN, 0, 0);
    Debug_String(cb_cast(" "), 1, 0, 0);
    Form_Hex_Pad(buf, num, 8);
    Debug_Str(s_cast(buf));
}


//
//  Debug_Chars: C
// 
// Print a number of spaces.
//
void Debug_Chars(REBYTE chr, REBCNT num)
{
    REBYTE spaces[100];

    memset(spaces, chr, MIN(num, 99));
    spaces[num] = 0;
    Debug_String(spaces, num, 0, 0);
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
//  Debug_Word: C
// 
// Print a REBOL word.
//
void Debug_Word(const REBVAL *word)
{
    Debug_Str(cs_cast(Get_Word_Name(word)));
}


//
//  Debug_Type: C
// 
// Print a REBOL datatype name.
//
void Debug_Type(const REBVAL *value)
{
    if (VAL_TYPE(value) < REB_MAX) Debug_Str(cs_cast(Get_Type_Name(value)));
    else Debug_Str("TYPE?!");
}


//
//  Debug_Value: C
//
void Debug_Value(const REBVAL *value, REBCNT limit, REBOOL mold)
{
    Print_Value(value, limit, mold); // higher level!
}


//
//  Debug_Values: C
//
void Debug_Values(const REBVAL *value, REBCNT count, REBCNT limit)
{
    REBSER *out;
    REBCNT i1;
    REBCNT i2;
    REBUNI uc, pc = ' ';
    REBCNT n;

    for (n = 0; n < count; n++, value++) {
        Debug_Space(1);
        if (n > 0 && VAL_TYPE(value) <= REB_NONE) Debug_Chars('.', 1);
        else {
            out = Mold_Print_Value(value, limit, TRUE); // shared mold buffer
            for (i1 = i2 = 0; i1 < out->tail; i1++) {
                uc = GET_ANY_CHAR(out, i1);
                if (uc < ' ') uc = ' ';
                if (uc > ' ' || pc > ' ') SET_ANY_CHAR(out, i2++, uc);
                pc = uc;
            }
            SET_ANY_CHAR(out, i2, 0);
            Debug_String(out->data, i2, TRUE, 0);
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
void Debug_Buf(const char *fmt, va_list *args)
{
    REBSER *buf = BUF_PRINT;
    REBCNT len;
    REBCNT n;
    REBYTE *bp;
    REBCNT tail;
    REBINT disabled = GC_Disabled;

    if (!buf) panic (Error(RE_NO_BUFFER));

    GC_Disabled = 1;

    RESET_SERIES(buf);

    // Limits output to size of buffer, will not expand it:
    bp = Form_Args_Core(STR_HEAD(buf), SERIES_REST(buf) - 1, fmt, args);
    tail = bp - STR_HEAD(buf);

    for (n = 0; n < tail; n += len) {
        len = LEN_BYTES(STR_SKIP(buf, n));
        if (len > 1024) len = 1024;
        Debug_String(STR_SKIP(buf, n), len, 0, 0);
    }

    assert(GC_Disabled == 1);
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
    va_list args;
    va_start(args, fmt);
    Debug_Buf(fmt, &args);
    va_end(args);
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


#if !defined(NDEBUG)

//
//  Probe_Core_Debug: C
// 
// Debug function for outputting a value.  Done as a function
// instead of just a macro due to how easy it is with varargs
// to order the types of the parameters wrong.  :-/
//
void Probe_Core_Debug(const char *msg, const char *file, int line, const REBVAL *val)
{
    if (msg)
        Debug_Fmt("\n** PROBE_MSG(\"%s\") %s:%d\n%r\n", msg, file, line, val);
    else
        Debug_Fmt("\n** PROBE() %s:%d\n%r\n", file, line, val);
}

#endif


//
//  Echo_File: C
//
REBFLG Echo_File(REBCHR *file)
{
    Req_SIO->special.file.path = file;
    return (DR_ERROR != OS_DO_DEVICE(Req_SIO, RDC_CREATE));
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
    REBYTE buffer[MAX_HEX_LEN+4];
    REBYTE *bp = buffer + MAX_HEX_LEN + 1;
    REBI64 sgn;

    // !!! val parameter was REBI64 at one point; changed to REBI64
    // as this does signed comparisons (val < 0 was never true...)
    sgn = (val < 0) ? -1 : 0;

    len = MIN(len, MAX_HEX_LEN);
    *bp-- = 0;
    while (val != sgn && len > 0) {
        *bp-- = Hex_Digits[val & 0xf];
        val >>= 4;
        len--;
    }
    for (; len > 0; len--) *bp-- = (sgn != 0) ? 'F' : '0';
    bp++;
    while ((*buf++ = *bp++));
    return buf-1;
}


//
//  Form_Hex2: C
// 
// Convert byte-sized int to xx format. Very fast.
//
REBYTE *Form_Hex2(REBYTE *bp, REBCNT val)
{
    bp[0] = Hex_Digits[(val & 0xf0) >> 4];
    bp[1] = Hex_Digits[val & 0xf];
    bp[2] = 0;
    return bp+2;
}


//
//  Form_Hex2_Uni: C
// 
// Convert byte-sized int to unicode xx format. Very fast.
//
REBUNI *Form_Hex2_Uni(REBUNI *up, REBCNT val)
{
    up[0] = Hex_Digits[(val & 0xf0) >> 4];
    up[1] = Hex_Digits[val & 0xf];
    up[2] = 0;
    return up+2;
}


//
//  Form_Hex_Esc_Uni: C
// 
// Convert byte int to %xx format (in unicode destination)
//
REBUNI *Form_Hex_Esc_Uni(REBUNI *up, REBUNI c)
{
    up[0] = '%';
    up[1] = Hex_Digits[(c & 0xf0) >> 4];
    up[2] = Hex_Digits[c & 0xf];
    up[3] = 0;
    return up+3;
}


//
//  Form_RGB_Uni: C
// 
// Convert 24 bit RGB to xxxxxx format.
//
REBUNI *Form_RGB_Uni(REBUNI *up, REBCNT val)
{
#ifdef ENDIAN_LITTLE
    up[0] = Hex_Digits[(val >>  4) & 0xf];
    up[1] = Hex_Digits[val & 0xf];
    up[2] = Hex_Digits[(val >> 12) & 0xf];
    up[3] = Hex_Digits[(val >>  8) & 0xf];
    up[4] = Hex_Digits[(val >> 20) & 0xf];
    up[5] = Hex_Digits[(val >> 16) & 0xf];
#else
    up[0] = Hex_Digits[(val >>  28) & 0xf];
    up[1] = Hex_Digits[(val >> 24) & 0xf];
    up[2] = Hex_Digits[(val >> 20) & 0xf];
    up[3] = Hex_Digits[(val >> 16) & 0xf];
    up[4] = Hex_Digits[(val >> 12) & 0xf];
    up[5] = Hex_Digits[(val >>  8) & 0xf];
#endif
    up[6] = 0;

    return up+6;
}


//
//  Form_Uni_Hex: C
// 
// Fast var-length hex output for uni-chars.
// Returns next position (just past the insert).
//
REBUNI *Form_Uni_Hex(REBUNI *out, REBCNT n)
{
    REBUNI buffer[10];
    REBUNI *up = &buffer[10];

    while (n != 0) {
        *(--up) = Hex_Digits[n & 0xf];
        n >>= 4;
    }

    while (up < &buffer[10]) *out++ = *up++;

    return out;
}


//
//  Form_Args_Core: C
// 
// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
// 
// Lower level (debugging) value formatter.
// Can restrict to max char size.
//
REBYTE *Form_Args_Core(REBYTE *bp, REBCNT max, const char *fmt, va_list *args)
{
    REBYTE *cp;
    REBCNT len = 0;
    REBINT pad;
    REBVAL *vp;
    REBYTE desc;
    REBSER *ser;
    REBVAL value;
    REBYTE padding;
    REBINT l;
    REBCNT ul;

    max--; // adjust for the fact that it adds a NULL at the end.

    //*bp++ = '!'; len++;

    for (; *fmt && len < max; fmt++) {

        // Copy string until next % escape:
        for (; *fmt && *fmt != '%' && len < max; len++) *bp++ = *fmt++;
        if (*fmt != '%') break;

        pad = 1;
        padding = ' ';
        fmt++; // skip %

pick:
        switch (desc = *fmt) {

        case '0':
            padding = '0';
        case '-':
        case '1':   case '2':   case '3':   case '4':
        case '5':   case '6':   case '7':   case '8':   case '9':
            fmt = cs_cast(Grab_Int(cb_cast(fmt), &pad));
            goto pick;

        case 'd':
            // All va_arg integer arguments will be coerced to platform 'int'
            cp = Form_Int_Pad(
                bp, cast(REBI64, va_arg(*args, int)), max-len, pad, padding
            );
            len += (REBCNT)(cp - bp);
            bp = cp;
            break;

        case 'D':
            // All va_arg integer arguments will be coerced to platform 'int'
            cp = Form_Int_Pad(
                bp, cast(REBI64, va_arg(*args, int)), max-len, pad, padding
            );
            len += (REBCNT)(cp - bp);
            bp = cp;
            break;

        case 's':
            cp = va_arg(*args, REBYTE *);
            if (pad == 1) pad = LEN_BYTES(cp);
            if (pad < 0) {
                pad = -pad;
                pad -= LEN_BYTES(cp);
                for (; pad > 0 && len < max; len++, pad--) *bp++ = ' ';
            }
            for (; *cp && len < max && pad > 0; pad--, len++) *bp++ = *cp++;
            for (; pad > 0 && len < max; len++, pad--) *bp++ = ' ';
            break;

        case 'r':   // use Mold
        case 'v':   // use Form
            vp = va_arg(*args, REBVAL *);
mold_value:
            // Form the REBOL value into a reused buffer:
            ser = Mold_Print_Value(vp, 0, desc != 'v');

            l = max - len - 1;
            if (pad != 1 && l > pad) l = pad;

            ul = SERIES_LEN(ser);
            l = Encode_UTF8(bp, l, UNI_HEAD(ser), &ul, OPT_ENC_UNISRC);
            len += l;

            // Filter out CTRL chars:
            for (; l > 0; l--, bp++) if (*bp < ' ') *bp = ' ';
            break;

        case 'm':  // Mold a series
            ser = va_arg(*args, REBSER *);
            // Val_Init_Block would Ensure_Series_Managed, we use a raw
            // VAL_SET instead
            VAL_SET(&value, REB_BLOCK);
            VAL_SERIES(&value) = ser;
            VAL_INDEX(&value) = 0;
            vp = &value;
            goto mold_value;

        case 'c':
            if (len < max) {
                *bp++ = cast(REBYTE, va_arg(*args, REBINT));
                len++;
            }
            break;

        case 'x':
            if (len + MAX_HEX_LEN + 1 < max) { // A cheat, but it is safe.
                *bp++ = '#';
                if (pad == 1) pad = 8;
                cp = Form_Hex_Pad(
                    bp, cast(REBU64, cast(REBUPT, va_arg(*args, REBYTE*))), pad
                );
                len += 1 + (REBCNT)(cp - bp);
                bp = cp;
            }
            break;

        default:
            *bp++ = *fmt;
            len++;
        }
    }
    *bp = 0;
    return bp;
}


//
//  Form_Args: C
//
REBYTE *Form_Args(REBYTE *bp, REBCNT max, const char *fmt, ...)
{
    REBYTE *result;
    va_list args;

    va_start(args, fmt);
    result = Form_Args_Core(bp, max, fmt, &args);
    va_end(args);

    return result;
}


/***********************************************************************
**
**  User Output Print Interface
**
***********************************************************************/

//
//  Prin_Value: C
// 
// Print a value or block's contents for user viewing.
// Can limit output to a given size. Set limit to 0 for full size.
//
void Prin_Value(const REBVAL *value, REBCNT limit, REBOOL mold)
{
    REBSER *out = Mold_Print_Value(value, limit, mold);
    Prin_OS_String(out->data, out->tail, OPT_ENC_UNISRC | OPT_ENC_CRLF_MAYBE);
}


//
//  Print_Value: C
// 
// Print a value or block's contents for user viewing.
// Can limit output to a given size. Set limit to 0 for full size.
//
void Print_Value(const REBVAL *value, REBCNT limit, REBOOL mold)
{
    Prin_Value(value, limit, mold);
    Print_OS_Line();
}


//
//  Init_Raw_Print: C
// 
// Initialize print module.
//
void Init_Raw_Print(void)
{
    Set_Root_Series(TASK_BUF_PRINT, Make_Binary(1000), "print buffer");
    Set_Root_Series(TASK_BUF_FORM,  Make_Binary(64), "form buffer");
}
