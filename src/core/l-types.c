//
//  File: %l-types.c
//  Summary: "special lexical type converters"
//  Section: lexical
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

#include "sys-core.h"
#include "sys-deci-funcs.h"
#include "sys-dec-to-char.h"
#include <errno.h>


//
// The scanning code in R3-Alpha used NULL to return failure during the scan
// of a value, possibly leaving the value itself in an incomplete or invalid
// state.  Rather than write stray incomplete values into these spots, Ren-C
// puts "unreadable blank"
//

#define return_NULL \
    do { Init_Unreadable_Blank(out); return NULL; } while (TRUE)


//
//  MAKE_Fail: C
//
void MAKE_Fail(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    UNUSED(out);
    UNUSED(kind);
    UNUSED(arg);

    fail ("Datatype does not have a MAKE handler registered");
}


//
//  MAKE_Unhooked: C
//
// MAKE STRUCT! is part of the FFI extension, but since user defined types
// aren't ready yet as a general concept, this hook is overwritten in the
// dispatch table when the extension loads.
//
void MAKE_Unhooked(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    UNUSED(out);
    UNUSED(arg);

    REBVAL *type = Get_Type(kind);
    UNUSED(type); // put in error message?

    fail ("Datatype is provided by an extension that's not currently loaded");
}


//
//  make: native [
//
//  {Constructs or allocates the specified datatype.}
//
//      return: [any-value!]
//          {Constructed value.}
//      type [any-value!]
//          {The datatype -or- an examplar value of the type to construct}
//      def [any-value!]
//          {Definition or size of the new value (binding may be modified)}
//  ]
//
REBNATIVE(make)
{
    INCLUDE_PARAMS_OF_MAKE;

    REBVAL *type = ARG(type);
    REBVAL *arg = ARG(def);

#if !defined(NDEBUG)
    if (IS_GOB(type)) {
        //
        // !!! It appears that GOBs had some kind of inheritance mechanism, by
        // which you would write:
        //
        //     gob1: make gob! [...]
        //     gob2: make gob1 [...]
        //
        // The new plan is that MAKE operates on a definition spec, and that
        // this type slot is always a value or exemplar.  So if the feature
        // is needed, it should be something like:
        //
        //     gob1: make gob! [...]
        //     gob2: make gob! [gob1 ...]
        //
        // Or perhaps not use make at all, but some other operation.
        //
        assert(FALSE);
    }
    else if (IS_EVENT(type)) {
        assert(FALSE); // ^-- same for events (?)
    }
#endif

    enum Reb_Kind kind;
    if (IS_DATATYPE(type))
        kind = VAL_TYPE_KIND(type);
    else
        kind = VAL_TYPE(type);

    MAKE_FUNC dispatcher = Make_Dispatch[kind];
    if (dispatcher == NULL)
        fail (Error_Bad_Make(kind, arg));

    if (IS_VARARGS(arg)) {
        //
        // Converting a VARARGS! to an ANY-ARRAY! involves spooling those
        // varargs to the end and making an array out of that.  It's not known
        // how many elements that will be, so they're gathered to the data
        // stack to find the size, then an array made.  Note that | will stop
        // varargs gathering.
        //
        // !!! MAKE should likely not be allowed to THROW in the general
        // case--especially if it is the implementation of construction
        // syntax (arbitrary code should not run during LOAD).  Since
        // vararg spooling may involve evaluation (e.g. to create an array)
        // it may be a poor fit for the MAKE umbrella.
        //
        // Temporarily putting the code here so that the make dispatchers
        // do not have to bubble up throws, but it is likely that this
        // should not have been a MAKE operation in the first place.
        //
        // !!! This MAKE will be destructive to its input (the varargs will
        // be fetched and exhausted).  That's not necessarily obvious, but
        // with a TO conversion it would be even less obvious...
        //
        if (dispatcher != &MAKE_Array)
            fail (Error_Bad_Make(kind, arg));

        // If there's any chance that the argument could produce voids, we
        // can't guarantee an array can be made out of it.
        //
        if (arg->payload.varargs.facade == NULL) {
            //
            // A vararg created from a block AND never passed as an argument
            // so no typeset or quoting settings available.  Can't produce
            // any voids, because the data source is a block.
            //
            assert(
                NOT_SER_FLAG(
                    arg->extra.binding, ARRAY_FLAG_VARLIST
                )
            );
        }
        else {
            REBCTX *context = CTX(arg->extra.binding);
            REBFRM *param_frame = CTX_FRAME_IF_ON_STACK(context);

            // If the VARARGS! has a call frame, then ensure that the call
            // frame where the VARARGS! originated is still on the stack.
            //
            if (param_frame == NULL)
                fail (Error_Varargs_No_Stack_Raw());

            REBVAL *param = FUNC_FACADE_HEAD(param_frame->phase)
                + arg->payload.varargs.param_offset;

            if (TYPE_CHECK(param, REB_MAX_VOID))
                fail (Error_Void_Vararg_Array_Raw());
        }

        REBDSP dsp_orig = DSP;

        do {
            REB_R r = Do_Vararg_Op_May_Throw(D_OUT, arg, VARARG_OP_TAKE);

            if (r == R_OUT_IS_THROWN) {
                DS_DROP_TO(dsp_orig);
                return R_OUT_IS_THROWN;
            }
            if (r == R_VOID)
                break;
            assert(r == R_OUT);

            DS_PUSH(D_OUT);
        } while (TRUE);

        Init_Any_Array(D_OUT, kind, Pop_Stack_Values(dsp_orig));
        return R_OUT;
    }

    dispatcher(D_OUT, kind, arg); // may fail()
    return R_OUT;
}


//
//  TO_Fail: C
//
void TO_Fail(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    UNUSED(out);
    UNUSED(kind);
    UNUSED(arg);

    fail ("Cannot convert to datatype");
}


//
//  TO_Unhooked: C
//
void TO_Unhooked(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    UNUSED(out);
    UNUSED(arg);

    REBVAL *type = Get_Type(kind);
    UNUSED(type); // use in error message?

    fail ("Datatype does not have extension with a TO handler registered");
}


//
//  to: native [
//
//  {Converts to a specified datatype.}
//
//      type [any-value!]
//          {The datatype -or- an exemplar value of the target type}
//      value [any-value!]
//          {The source value to convert}
//  ]
//
REBNATIVE(to)
{
    INCLUDE_PARAMS_OF_TO;

    REBVAL *type = ARG(type);
    REBVAL *arg = ARG(value);

    enum Reb_Kind kind;
    if (IS_DATATYPE(type))
        kind = VAL_TYPE_KIND(type);
    else
        kind = VAL_TYPE(type);

    TO_FUNC dispatcher = To_Dispatch[kind];
    if (dispatcher == NULL)
        fail (arg);

    dispatcher(D_OUT, kind, arg); // may fail();
    return R_OUT;
}


//
//  REBTYPE: C
//
// There's no actual "Unhooked" data type, it is used as a placeholder for
// if a datatype (such as STRUCT!) is going to have its behavior loaded by
// an extension.
//
REBTYPE(Unhooked)
{
    UNUSED(frame_);
    UNUSED(action);

    fail ("Datatype does not have its REBTYPE() handler loaded by extension");
}


//
//  Scan_Hex: C
//
// Scans hex while it is valid and does not exceed the maxlen.
// If the hex string is longer than maxlen - it's an error.
// If a bad char is found less than the minlen - it's an error.
// String must not include # - ~ or other invalid chars.
// If minlen is zero, and no string, that's a valid zero value.
//
// Note, this function relies on LEX_WORD lex values having a LEX_VALUE
// field of zero, except for hex values.
//
const REBYTE *Scan_Hex(
    REBVAL *out,
    const REBYTE *cp,
    REBCNT minlen,
    REBCNT maxlen
) {
    TRASH_CELL_IF_DEBUG(out);

    if (maxlen > MAX_HEX_LEN)
        return_NULL;

    REBI64 i = 0;
    REBCNT cnt = 0;
    REBYTE lex;
    while ((lex = Lex_Map[*cp]) > LEX_WORD) {
        REBYTE v;
        if (++cnt > maxlen)
            return_NULL;
        v = cast(REBYTE, lex & LEX_VALUE); // char num encoded into lex
        if (!v && lex < LEX_NUMBER)
            return_NULL;  // invalid char (word but no val)
        i = (i << 4) + v;
        cp++;
    }

    if (cnt < minlen)
        return_NULL;

    Init_Integer(out, i);
    return cp;
}


//
//  Scan_Hex2: C
//
// Decode a %xx hex encoded byte into a char.
//
// The % should already be removed before calling this.
//
// We don't allow a %00 in files, urls, email, etc... so
// a return of 0 is used to indicate an error.
//
REBOOL Scan_Hex2(REBUNI *out, const void *p, REBOOL unicode)
{
    REBUNI c1;
    REBUNI c2;
    if (unicode) {
        const REBUNI *up = cast(const REBUNI*, p);
        c1 = up[0];
        c2 = up[1];
    }
    else {
        const REBYTE *bp = cast(const REBYTE*, p);
        c1 = bp[0];
        c2 = bp[1];
    }

    REBYTE lex1 = Lex_Map[c1];
    REBYTE d1 = lex1 & LEX_VALUE;
    if (lex1 < LEX_WORD || (d1 == 0 && lex1 < LEX_NUMBER))
        return FALSE;

    REBYTE lex2 = Lex_Map[c2];
    REBYTE d2 = lex2 & LEX_VALUE;
    if (lex2 < LEX_WORD || (d2 == 0 && lex2 < LEX_NUMBER))
        return FALSE;

    *out = cast(REBUNI, (d1 << 4) + d2);

    return TRUE;
}


//
//  Scan_Dec_Buf: C
//
// Validate a decimal number. Return on first invalid char (or end).
// Returns NULL if not valid.
//
// Scan is valid for 1 1.2 1,2 1'234.5 1x 1.2x 1% 1.2% etc.
//
// !!! Is this redundant with Scan_Decimal?  Appears to be similar code.
//
const REBYTE *Scan_Dec_Buf(
    REBYTE *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len // max size of buffer
) {
    assert(len >= MAX_NUM_LEN);

    REBYTE *bp = out;
    REBYTE *be = bp + len - 1;

    if (*cp == '+' || *cp == '-')
        *bp++ = *cp++;

    REBOOL digit_present = FALSE;
    while (IS_LEX_NUMBER(*cp) || *cp == '\'') {
        if (*cp != '\'') {
            *bp++ = *cp++;
            if (bp >= be)
                return NULL;
            digit_present = TRUE;
        }
        else
            ++cp;
    }

    if (*cp == ',' || *cp == '.')
        cp++;

    *bp++ = '.';
    if (bp >= be)
        return NULL;

    while (IS_LEX_NUMBER(*cp) || *cp == '\'') {
        if (*cp != '\'') {
            *bp++ = *cp++;
            if (bp >= be)
                return NULL;
            digit_present = TRUE;
        }
        else
            ++cp;
    }

    if (NOT(digit_present))
        return NULL;

    if (*cp == 'E' || *cp == 'e') {
        *bp++ = *cp++;
        if (bp >= be)
            return NULL;

        digit_present = FALSE;

        if (*cp == '-' || *cp == '+') {
            *bp++ = *cp++;
            if (bp >= be)
                return NULL;
        }

        while (IS_LEX_NUMBER(*cp)) {
            *bp++ = *cp++;
            if (bp >= be)
                return NULL;
            digit_present = TRUE;
        }

        if (NOT(digit_present))
            return NULL;
    }

    *bp = '\0';
    return cp;
}


//
//  Scan_Decimal: C
//
// Scan and convert a decimal value.  Return zero if error.
//
const REBYTE *Scan_Decimal(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len,
    REBOOL dec_only
) {
    TRASH_CELL_IF_DEBUG(out);

    REBYTE buf[MAX_NUM_LEN + 4];
    REBYTE *ep = buf;
    if (len > MAX_NUM_LEN)
        return_NULL;

    const REBYTE *bp = cp;

    if (*cp == '+' || *cp == '-')
        *ep++ = *cp++;

    REBOOL digit_present = FALSE;

    while (IS_LEX_NUMBER(*cp) || *cp == '\'') {
        if (*cp != '\'') {
            *ep++ = *cp++;
            digit_present = TRUE;
        }
        else
            ++cp;
    }

    if (*cp == ',' || *cp == '.')
        ++cp;

    *ep++ = '.';

    while (IS_LEX_NUMBER(*cp) || *cp == '\'') {
        if (*cp != '\'') {
            *ep++ = *cp++;
            digit_present = TRUE;
        }
        else
            ++cp;
    }

    if (NOT(digit_present))
        return_NULL;

    if (*cp == 'E' || *cp == 'e') {
        *ep++ = *cp++;
        digit_present = FALSE;

        if (*cp == '-' || *cp == '+')
            *ep++ = *cp++;

        while (IS_LEX_NUMBER(*cp)) {
            *ep++ = *cp++;
            digit_present = TRUE;
        }

        if (NOT(digit_present))
            return_NULL;
    }

    if (*cp == '%') {
        if (dec_only)
            return_NULL;

        ++cp; // ignore it
    }

    *ep = '\0';

    if (cast(REBCNT, cp - bp) != len)
        return_NULL;

    VAL_RESET_HEADER(out, REB_DECIMAL);

    const char *se;
    VAL_DECIMAL(out) = STRTOD(s_cast(buf), &se);

    // !!! TBD: need check for NaN, and INF

    if (fabs(VAL_DECIMAL(out)) == HUGE_VAL)
        fail (Error_Overflow_Raw());

    return cp;
}


//
//  Scan_Integer: C
//
// Scan and convert an integer value.  Return zero if error.
// Allow preceding + - and any combination of ' marks.
//
const REBYTE *Scan_Integer(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    // Super-fast conversion of zero and one (most common cases):
    if (len == 1) {
        if (*cp == '0') {
            Init_Integer(out, 0);
            return cp + 1;
        }
        if (*cp == '1') {
            Init_Integer(out, 1);
            return cp + 1;
         }
    }

    REBYTE buf[MAX_NUM_LEN + 4];
    if (len > MAX_NUM_LEN)
        return_NULL; // prevent buffer overflow

    REBYTE *bp = buf;

    REBOOL neg = FALSE;

    REBINT num = cast(REBINT, len);

    // Strip leading signs:
    if (*cp == '-') {
        *bp++ = *cp++;
        --num;
        neg = TRUE;
    }
    else if (*cp == '+') {
        ++cp;
        --num;
    }

    // Remove leading zeros:
    for (; num > 0; num--) {
        if (*cp == '0' || *cp == '\'')
            ++cp;
        else
            break;
    }

    if (num == 0) { // all zeros or '
        // return early to avoid platform dependant error handling in CHR_TO_INT
        Init_Integer(out, 0);
        return cp;
    }

    // Copy all digits, except ' :
    for (; num > 0; num--) {
        if (*cp >= '0' && *cp <= '9')
            *bp++ = *cp++;
        else if (*cp == '\'')
            ++cp;
        else
            return_NULL;
    }
    *bp = '\0';

    // Too many digits?
    len = bp - &buf[0];
    if (neg)
        --len;
    if (len > 19) {
        // !!! magic number :-( How does it relate to MAX_INT_LEN (also magic)
        return_NULL;
    }

    // Convert, check, and return:
    errno = 0;

    VAL_RESET_HEADER(out, REB_INTEGER);

    VAL_INT64(out) = CHR_TO_INT(buf);
    if (errno != 0)
        return_NULL; // overflow

    if ((VAL_INT64(out) > 0 && neg) || (VAL_INT64(out) < 0 && !neg))
        return_NULL;

    return cp;
}


//
//  Scan_Money: C
//
// Scan and convert money.  Return zero if error.
//
const REBYTE *Scan_Money(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    const REBYTE *end;

    if (*cp == '$') {
        ++cp;
        --len;
    }
    if (len == 0)
        return_NULL;

    Init_Money(out, string_to_deci(cp, &end));
    if (end != cp + len)
        return_NULL;

    return end;
}


//
//  Scan_Date: C
//
// Scan and convert a date. Also can include a time and zone.
//
const REBYTE *Scan_Date(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    const REBYTE *end = cp + len;

    // Skip spaces:
    for (; *cp == ' ' && cp != end; cp++);

    // Skip day name, comma, and spaces:
    const REBYTE *ep;
    for (ep = cp; *ep != ',' && ep != end; ep++);
    if (ep != end) {
        cp = ep + 1;
        while (*cp == ' ' && cp != end) cp++;
    }
    if (cp == end)
        return_NULL;

    REBINT num;

    // Day or 4-digit year:
    ep = Grab_Int(cp, &num);
    if (num < 0)
        return_NULL;

    REBINT day;
    REBINT month;
    REBINT year;
    REBINT tz;

    REBCNT size = cast(REBCNT, ep - cp);
    if (size >= 4) {
        // year is set in this branch (we know because day is 0)
        // Ex: 2009/04/20/19:00:00+0:00
        year = num;
        day = 0;
    }
    else if (size) {
        // year is not set in this branch (we know because day ISN'T 0)
        // Ex: 12-Dec-2012
        day = num;
        if (day == 0)
            return_NULL;

        // !!! Clang static analyzer doesn't know from test of `day` below
        // how it connects with year being set or not.  Suppress warning.
        year = MIN_I32; // !!! Garbage, should not be read.
    }
    else
        return_NULL;

    cp = ep;

    // Determine field separator:
    if (*cp != '/' && *cp != '-' && *cp != '.' && *cp != ' ')
        return_NULL;

    REBYTE sep = *cp++;

    // Month as number or name:
    ep = Grab_Int(cp, &num);
    if (num < 0)
        return_NULL;

    size = cast(REBCNT, ep - cp);

    if (size > 0)
        month = num; // got a number
    else { // must be a word
        for (ep = cp; IS_LEX_WORD(*ep); ep++)
            NOOP; // scan word

        size = cast(REBCNT, ep - cp);
        if (size < 3)
            return_NULL;

        for (num = 0; num < 12; num++) {
            if (!Compare_Bytes(cb_cast(Month_Names[num]), cp, size, TRUE))
                break;
        }
        month = num + 1;
    }

    if (month < 1 || month > 12)
        return_NULL;

    cp = ep;
    if (*cp++ != sep)
        return_NULL;

    // Year or day (if year was first):
    ep = Grab_Int(cp, &num);
    if (*cp == '-' || num < 0)
        return_NULL;

    size = cast(REBCNT, ep - cp);
    if (size == 0)
        return_NULL;

    if (day == 0) {
        // year already set, but day hasn't been
        day = num;
    }
    else {
        // day has been set, but year hasn't been.
        if (size >= 3)
            year = num;
        else {
            // !!! Originally this allowed shorthands, so that 96 = 1996, etc.
            //
            //     if (num >= 70)
            //         year = 1900 + num;
            //     else
            //         year = 2000 + num;
            //
            // It was trickier than that, because it actually used the current
            // year (from the clock) to guess what the short year meant.  That
            // made it so the scanner would scan the same source code
            // differently based on the clock, which is bad.  By allowing
            // short dates to be turned into their short year equivalents, the
            // user code can parse such dates and fix them up after the fact
            // according to their requirements, `if date/year < 100 [...]`
            //
            year = num;
        }
    }

    if (year > MAX_YEAR || day < 1 || day > Month_Max_Days[month-1])
        return_NULL;

    // Check February for leap year or century:
    if (month == 2 && day == 29) {
        if (
            ((year % 4) != 0) ||        // not leap year
            ((year % 100) == 0 &&       // century?
            (year % 400) != 0)
        ){
            return_NULL; // not leap century
        }
    }

    cp = ep;

    if (cp >= end) {
        VAL_RESET_HEADER(out, REB_DATE);
        goto end_date; // needs header set
    }

    if (*cp == '/' || *cp == ' ') {
        sep = *cp++;

        if (cp >= end) {
            VAL_RESET_HEADER(out, REB_DATE);
            goto end_date; // needs header set
        }

        cp = Scan_Time(out, cp, 0);
        if (
            cp == NULL
            || !IS_TIME(out)
            || (VAL_NANO(out) < 0)
            || (VAL_NANO(out) >= SECS_TO_NANO(24 * 60 * 60))
        ){
            return_NULL;
        }

        VAL_RESET_HEADER_EXTRA(out, REB_DATE, DATE_FLAG_HAS_TIME);
    }
    else
        VAL_RESET_HEADER(out, REB_DATE); // no DATE_FLAG_HAS_TIME

    // past this point, header is set, so `goto end_date` is legal.

    if (*cp == sep)
        ++cp;

    // Time zone can be 12:30 or 1230 (optional hour indicator)
    if (*cp == '-' || *cp == '+') {
        if (cp >= end)
            goto end_date;

        ep = Grab_Int(cp + 1, &num);
        if (ep - cp == 0)
            return_NULL;

        if (*ep != ':') {
            if (num < -1500 || num > 1500)
                return_NULL;

            int h = (num / 100);
            int m = (num - (h * 100));

            tz = (h * 60 + m) / ZONE_MINS;
        }
        else {
            if (num < -15 || num > 15)
                return_NULL;

            tz = num * (60 / ZONE_MINS);

            if (*ep == ':') {
                ep = Grab_Int(ep + 1, &num);
                if (num % ZONE_MINS != 0)
                    return_NULL;

                tz += num / ZONE_MINS;
            }
        }

        if (ep != end)
            return_NULL;

        if (*cp == '-')
            tz = -tz;

        cp = ep;

        SET_VAL_FLAG(out, DATE_FLAG_HAS_ZONE);
        INIT_VAL_ZONE(out, tz);
    }

end_date:
    assert(IS_DATE(out)); // don't reset header here; overwrites flags
    VAL_YEAR(out)  = year;
    VAL_MONTH(out) = month;
    VAL_DAY(out) = day;

    // if VAL_NANO() was set, then DATE_FLAG_HAS_TIME should be true
    // if VAL_ZONE() was set, then DATE_FLAG_HAS_ZONE should be true

    // This step used to be skipped if tz was 0, but now that is a
    // state distinguished from "not having a time zone"
    //
    Adjust_Date_Zone(out, TRUE);

    return cp;
}


//
//  Scan_File: C
//
// Scan and convert a file name.
//
const REBYTE *Scan_File(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    if (*cp == '%') {
        cp++;
        len--;
    }

    REBUNI term = 0;
    const REBYTE *invalid;
    if (*cp == '"') {
        cp++;
        len--;
        term = '"';
        invalid = cb_cast(":;\"");
    }
    else {
        term = 0;
        invalid = cb_cast(":;()[]\"");
    }

    DECLARE_MOLD (mo);

    cp = Scan_Item_Push_Mold(mo, cp, cp + len, term, invalid);
    if (cp == NULL) {
        Drop_Mold(mo);
        return_NULL;
    }

    Init_File(out, Pop_Molded_String(mo));
    return cp;
}


//
//  Scan_Email: C
//
// Scan and convert email.
//
const REBYTE *Scan_Email(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    REBSER *s = Make_Unicode(len);
    REBCHR(*) up = UNI_HEAD(s);

    REBCNT num_chars = 0;

    REBOOL found_at = FALSE;
    for (; len > 0; len--) {
        if (*cp == '@') {
            if (found_at)
                return_NULL;
            found_at = TRUE;
        }

        if (*cp == '%') {
            const REBOOL unicode = FALSE;
            REBUNI ch;
            if (len <= 2 || !Scan_Hex2(&ch, cp + 1, unicode))
                return_NULL;

            up = WRITE_CHR(up, ch);
            ++num_chars;

            cp += 3;
            len -= 2;
        }
        else {
            up = WRITE_CHR(up, *cp++);
            ++num_chars;
        }
    }

    if (NOT(found_at))
        return_NULL;

    TERM_UNI_LEN(s, num_chars);

    Init_Email(out, s);
    return cp;
}


//
//  Scan_URL: C
//
// While Rebol2, R3-Alpha, and Red attempted to apply some amount of decoding
// (e.g. how %20 is "space" in http:// URL!s), Ren-C leaves URLs "as-is".
// This means a URL may be copied from a web browser bar and pasted back.
// It also means that the URL may be used with custom schemes (odbc://...)
// that have different ideas of the meaning of characters like `%`.
//
// !!! The current concept is that URL!s typically represent the *decoded*
// forms, and thus express unicode codepoints normally...preserving either of:
//
//     https://duckduckgo.com/?q=hergé+&+tintin
//     https://duckduckgo.com/?q=hergé+%26+tintin
//
// Then, the encoded forms with UTF-8 bytes expressed in %XX form would be
// converted as STRING!, where their datatype suggests the encodedness:
//
//     {https://duckduckgo.com/?q=herg%C3%A9+%26+tintin}
//
// (This is similar to how local FILE!s, where e.g. slashes become backslash
// on Windows, are expressed as STRING!.)
//
const REBYTE *Scan_URL(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
){
    return Scan_Any(out, cp, len, REB_URL);
}


//
//  Scan_Pair: C
//
// Scan and convert a pair
//
const REBYTE *Scan_Pair(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    REBYTE buf[MAX_NUM_LEN + 4];

    const REBYTE *ep = Scan_Dec_Buf(&buf[0], cp, MAX_NUM_LEN);
    if (ep == NULL)
        return_NULL;
    if (*ep != 'x' && *ep != 'X')
        return_NULL;

    VAL_RESET_HEADER(out, REB_PAIR);
    out->payload.pair = Alloc_Pairing();
    VAL_RESET_HEADER(out->payload.pair, REB_DECIMAL);
    VAL_RESET_HEADER(PAIRING_KEY(out->payload.pair), REB_DECIMAL);

    VAL_PAIR_X(out) = cast(float, atof(cast(char*, &buf[0]))); //n;
    ep++;

    const REBYTE *xp = Scan_Dec_Buf(&buf[0], ep, MAX_NUM_LEN);
    if (!xp) {
        Free_Pairing(out->payload.pair);
        return_NULL;
    }

    VAL_PAIR_Y(out) = cast(float, atof(cast(char*, &buf[0]))); //n;

    if (len > cast(REBCNT, xp - cp)) {
        Free_Pairing(out->payload.pair);
        return_NULL;
    }

    Manage_Pairing(out->payload.pair);
    return xp;
}


//
//  Scan_Tuple: C
//
// Scan and convert a tuple.
//
const REBYTE *Scan_Tuple(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    if (len == 0)
        return_NULL;

    const REBYTE *ep;
    REBCNT size = 1;
    REBINT n;
    for (n = cast(REBINT, len), ep = cp; n > 0; n--, ep++) { // count '.'
        if (*ep == '.')
            ++size;
    }

    if (size > MAX_TUPLE)
        return_NULL;

    if (size < 3)
        size = 3;

    VAL_RESET_HEADER(out, REB_TUPLE);
    VAL_TUPLE_LEN(out) = cast(REBYTE, size);

    REBYTE *tp = VAL_TUPLE(out);
    memset(tp, 0, sizeof(REBTUP) - 2);

    for (ep = cp; len > cast(REBCNT, ep - cp); ++ep) {
        ep = Grab_Int(ep, &n);
        if (n < 0 || n > 255)
            return_NULL;

        *tp++ = cast(REBYTE, n);
        if (*ep != '.')
            break;
    }

    if (len > cast(REBCNT, ep - cp))
        return_NULL;

    return ep;
}


//
//  Scan_Binary: C
//
// Scan and convert binary strings.
//
const REBYTE *Scan_Binary(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    REBINT base = 16;

    if (*cp != '#') {
        const REBYTE *ep = Grab_Int(cp, &base);
        if (cp == ep || *ep != '#')
            return_NULL;
        len -= cast(REBCNT, ep - cp);
        cp = ep;
    }

    cp++;  // skip #
    if (*cp++ != '{')
        return_NULL;

    len -= 2;

    cp = Decode_Binary(out, cp, len, base, '}');
    if (cp == NULL)
        return_NULL;

    cp = Skip_To_Byte(cp, cp + len, '}');
    if (cp == NULL)
        return_NULL; // series will be gc'd

    return cp + 1; // include the "}" in the scan total
}


//
//  Scan_Any: C
//
// Scan any string that does not require special decoding.
//
const REBYTE *Scan_Any(
    REBVAL *out, // may live in data stack (do not call DS_PUSH, GC, eval)
    const REBYTE *cp,
    REBCNT num_bytes,
    enum Reb_Kind type
) {
    TRASH_CELL_IF_DEBUG(out);

    // The range for a curly braced string may span multiple lines, and some
    // files may have CR and LF in the data:
    //
    //     {line one ;-- imagine this is CR LF...not just LF
    //     line two}
    //
    // Despite the presence of the CR in the source file, the scanned literal
    // should only support LF (if it supports files with it at all)
    //
    // http://blog.hostilefork.com/death-to-carriage-return/
    //
    REBOOL crlf_to_lf = TRUE;

    REBSER *s = Append_UTF8_May_Fail(NULL, cs_cast(cp), num_bytes, crlf_to_lf);
    Init_Any_Series(out, type, s);

    return cp + num_bytes;
}


//
//  scan-net-header: native [
//      {Scan an Internet-style header (HTTP, SMTP).}
//
//      header [binary!]
//          {Fields with duplicate words will be merged into a block.}
//  ]
//
REBNATIVE(scan_net_header)
//
// !!! This routine used to be a feature of CONSTRUCT in R3-Alpha, and was
// used by %prot-http.r.  The idea was that instead of providing a parent
// object, a STRING! or BINARY! could be provided which would be turned
// into a block by this routine.
//
// It doesn't make much sense to have this coded in C rather than using PARSE
// It's only being converted into a native to avoid introducing bugs by
// rewriting it as Rebol in the middle of other changes.
{
    INCLUDE_PARAMS_OF_SCAN_NET_HEADER;

    REBARR *result = Make_Array(10); // Just a guess at size (use STD_BUF?)

    REBVAL *header = ARG(header);
    REBCNT index = VAL_INDEX(header);
    REBSER *utf8 = VAL_SERIES(header);

    INIT_VAL_SERIES(header, utf8); // GC protect, unnecessary?

    REBYTE *cp = BIN_HEAD(utf8) + index;

    while (IS_LEX_ANY_SPACE(*cp)) cp++; // skip white space

    REBYTE *start;
    REBINT len;

    while (TRUE) {
        // Scan valid word:
        if (IS_LEX_WORD(*cp)) {
            start = cp;
            while (
                IS_LEX_WORD_OR_NUMBER(*cp)
                || *cp == '.'
                || *cp == '-'
                || *cp == '_'
            ) {
                cp++;
            }
        }
        else break;

        if (*cp != ':')
            break;

        REBVAL *val = NULL; // rigorous checks worry it could be uninitialized

        REBSTR *name = Intern_UTF8_Managed(start, cp - start);
        RELVAL *item;

        cp++;
        // Search if word already present:
        for (item = ARR_HEAD(result); NOT_END(item); item += 2) {
            assert(IS_STRING(item + 1) || IS_BLOCK(item + 1));
            if (SAME_STR(VAL_WORD_SPELLING(item), name)) {
                // Does it already use a block?
                if (IS_BLOCK(item + 1)) {
                    // Block of values already exists:
                    val = Alloc_Tail_Array(VAL_ARRAY(item + 1));
                }
                else {
                    // Create new block for values:
                    REBARR *array = Make_Array(2);
                    Derelativize(
                        Alloc_Tail_Array(array),
                        item + 1, // prior value
                        SPECIFIED // no relative values added
                    );
                    val = Alloc_Tail_Array(array);
                    Init_Unreadable_Blank(val); // for Init_Block
                    Init_Block(item + 1, array);
                }
                break;
            }
        }

        if (IS_END(item)) { // didn't break, add space for new word/value
            Init_Set_Word(Alloc_Tail_Array(result), name);
            val = Alloc_Tail_Array(result);
        }

        while (IS_LEX_SPACE(*cp)) cp++;
        start = cp;
        len = 0;
        while (!ANY_CR_LF_END(*cp)) {
            len++;
            cp++;
        }
        // Is it continued on next line?
        while (*cp) {
            if (*cp == CR)
                ++cp;
            if (*cp == LF)
                ++cp;
            if (!IS_LEX_SPACE(*cp))
                break;
            while (IS_LEX_SPACE(*cp))
                ++cp;
            while (!ANY_CR_LF_END(*cp)) {
                ++len;
                ++cp;
            }
        }

        // Create string value (ignoring lines and indents):
        //
        // !!! This is written to deal with unicode lengths in terms of *size*
        // in bytes, not *length* in characters.  If it were to be done
        // correctly, it would need to use NEXT_CHR to count the characters
        // in the loop above.  Better to convert to usermode.

        REBSER *string = Make_Unicode(len);
        REBCHR(*) str = UNI_HEAD(string);
        cp = start;

        // "Code below *MUST* mirror that above:"

        while (!ANY_CR_LF_END(*cp))
            str = WRITE_CHR(str, *cp++);
        while (*cp != '\0') {
            if (*cp == CR)
                ++cp;
            if (*cp == LF)
                ++cp;
            if (!IS_LEX_SPACE(*cp))
                break;
            while (IS_LEX_SPACE(*cp))
                ++cp;
            while (!ANY_CR_LF_END(*cp))
                str = WRITE_CHR(str, *cp++);
        }
        TERM_UNI_LEN(string, len);
        Init_String(val, string);
    }

    Init_Block(D_OUT, result);
    return R_OUT;
}
