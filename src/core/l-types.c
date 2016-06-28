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
// Copyright 2012-2016 Rebol Open Source Contributors
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

extern const MAKE_FUNC Make_Dispatch[REB_MAX_0];

// !!! Actually should be a .inc file, as it includes data declarations
// Has a repeated prototype in %l-scan.c to avoid double inclusion
//
#include "tmp-maketypes.h"

//
//  make: native [
//
//  {Constructs or allocates the specified datatype.}
//
//      type [any-value!]
//          {The datatype -or- an examplar value of the type to construct}
//      def [any-value!]
//          {Definition or size of the new value (binding may be modified)}
//  ]
//
REBNATIVE(make)
{
    PARAM(1, type);
    PARAM(2, def);

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

    MAKE_FUNC dispatcher = Make_Dispatch[TO_0_FROM_KIND(kind)];
    if (dispatcher == NULL)
        fail (Error_Bad_Make(kind, arg));

    if (IS_VARARGS(arg)) {
        //
        // Converting a VARARGS! to an ANY-ARRAY! involves spooling those
        // varargs to the end and making an array out of that.  It's not known
        // how many elements that will be, so they're gathered to the data
        // stack to find the size, then an array made.  Note that | will stop
        // a normal or soft-quoted varargs, but a hard-quoted varargs will
        // grab all the values to the end of the source.
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

        const REBVAL *vararg_param;
        REBVAL *vararg_arg;

        REBVAL fake_param;

        REBARR *feed;

        if (GET_VAL_FLAG(arg, VARARGS_FLAG_NO_FRAME)) {
            feed = VAL_VARARGS_ARRAY1(arg);

            // Just a vararg created from a block, so no typeset or quoting
            // settings available.  Make a fake parameter that hard quotes
            // and takes any type (it will be type checked if in a chain).
            //
            Val_Init_Typeset(&fake_param, ALL_64, Canon(SYM_ELLIPSIS));
            INIT_VAL_PARAM_CLASS(&fake_param, PARAM_CLASS_HARD_QUOTE);
            vararg_param = &fake_param;
            vararg_arg = &fake_param; // doesn't matter, just gets flag set
        }
        else {
            feed = CTX_VARLIST(VAL_VARARGS_FRAME_CTX(arg));
            vararg_param = VAL_VARARGS_PARAM(arg);
            vararg_arg = VAL_VARARGS_ARG(arg);
        }

        REBDSP dsp_orig = DSP;

        do {
            REBIXO indexor = Do_Vararg_Op_Core(
                D_OUT, feed, vararg_param, vararg_arg, NULL, VARARG_OP_TAKE
            );

            if (indexor == THROWN_FLAG) {
                DS_DROP_TO(dsp_orig);
                return TRUE;
            }
            if (indexor == END_FLAG)
                break;
            assert(indexor == VA_LIST_FLAG);

            DS_PUSH(D_OUT);
        } while (TRUE);

        Val_Init_Array(D_OUT, kind, Pop_Stack_Values(dsp_orig));

        if (FALSE) {
            //
            // !!! If desired, input could be fed back into the varargs
            // after exhaustion by setting it up with array data as a new
            // subfeed.  Probably doesn't make sense to re-feed with data
            // that has been evaluated, and subfeeds can't be fixed up
            // like this either...disabled for now.
            //
            REBARR **subfeed_addr = SUBFEED_ADDR_OF_FEED(feed);
            assert(*subfeed_addr == NULL); // all values should be exhausted
            *subfeed_addr = Make_Singular_Array(D_OUT);
            MANAGE_ARRAY(*subfeed_addr);
            *SUBFEED_ADDR_OF_FEED(*subfeed_addr) = NULL;
        }

        return FALSE;
    }

    dispatcher(D_OUT, kind, arg); // may fail()
    return R_OUT;
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
    PARAM(1, type);
    PARAM(2, value);

    REBVAL *type = ARG(type);
    REBVAL *arg = ARG(value);

    enum Reb_Kind kind;
    if (IS_DATATYPE(type))
        kind = VAL_TYPE_KIND(type);
    else
        kind = VAL_TYPE(type);

    TO_FUNC dispatcher = To_Dispatch[TO_0_FROM_KIND(kind)];
    if (dispatcher == NULL)
        fail (Error_Invalid_Arg(arg));

    dispatcher(D_OUT, kind, arg); // may fail();
    return R_OUT;
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
const REBYTE *Scan_Hex(REBI64 *out, const REBYTE *cp, REBCNT minlen, REBCNT maxlen)
{
    REBYTE lex;
    REBCNT cnt = 0;

    if (maxlen > MAX_HEX_LEN) return NULL;

    *out = 0;
    while ((lex = Lex_Map[*cp]) > LEX_WORD) {
        REBYTE v;
        if (++cnt > maxlen) return NULL;
        v = cast(REBYTE, lex & LEX_VALUE); // char num encoded into lex
        if (!v && lex < LEX_NUMBER)
            return NULL;  // invalid char (word but no val)
        *out = (*out << 4) + v;
        cp++;
    }

    if (cnt < minlen) return 0;

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
REBOOL Scan_Hex2(const REBYTE *bp, REBUNI *n, REBOOL unicode)
{
    REBUNI c1, c2;
    REBYTE d1, d2;
    REBYTE lex;

    if (unicode) {
        const REBUNI *up = cast(const REBUNI*, bp);
        c1 = up[0];
        c2 = up[1];
    } else {
        c1 = bp[0];
        c2 = bp[1];
    }

    lex = Lex_Map[c1];
    d1 = lex & LEX_VALUE;
    if (lex < LEX_WORD || (!d1 && lex < LEX_NUMBER)) return FALSE;

    lex = Lex_Map[c2];
    d2 = lex & LEX_VALUE;
    if (lex < LEX_WORD || (!d2 && lex < LEX_NUMBER)) return FALSE;

    *n = (REBUNI)((d1 << 4) + d2);

    return TRUE;
}


//
//  Scan_Hex_Bytes: C
// 
// Low level conversion of hex chars into binary bytes.
// Returns the number of bytes in binary.
//
REBINT Scan_Hex_Bytes(REBVAL *val, REBCNT maxlen, REBYTE *out)
{
    REBYTE b, n = 0;
    REBCNT cnt;
    REBYTE lex;
    REBCNT len;
    REBUNI c;
    REBYTE *start = out;

    len = VAL_LEN_AT(val);
    if (len > maxlen) return 0;

    for (cnt = 0; cnt < len; cnt++) {
        c = GET_ANY_CHAR(VAL_SERIES(val), VAL_INDEX(val)+cnt);
        if (c > 127) return 0;
        lex = Lex_Map[c];
        b = (REBYTE)(lex & LEX_VALUE);   /* char num encoded into lex */
        if (!b && lex < LEX_NUMBER) return 0;  /* invalid char (word but no val) */
        if ((cnt + len) & 1) *out++ = (n << 4) + b; // cnt + len deals with odd # of chars
        else n = b & 15;
    }

    return (out - start);
}


//
//  Scan_Hex_Value: C
// 
// Given a string, scan it as hex. Chars can be 8 or 16 bit.
// Result is 32 bits max.
// Throw errors.
//
REBCNT Scan_Hex_Value(const void *p, REBCNT len, REBOOL unicode)
{
    REBUNI c;
    REBCNT n;
    REBYTE lex;
    REBCNT num = 0;
    const REBYTE *bp = unicode ? NULL : cast(const REBYTE *, p);
    const REBUNI *up = unicode ? cast(const REBUNI *, p) : NULL;

    if (len > 8) goto bad_hex;

    for (n = 0; n < len; n++) {
        c = unicode ? up[n] : cast(REBUNI, bp[n]);

        if (c > 255) goto bad_hex;

        lex = Lex_Map[c];
        if (lex <= LEX_WORD) goto bad_hex;

        c = lex & LEX_VALUE;
        if (!c && lex < LEX_NUMBER) goto bad_hex;
        num = (num << 4) + c;
    }
    return num;

bad_hex:
    fail (Error(RE_INVALID_CHARS));
}


//
//  Scan_Dec_Buf: C
// 
// Validate a decimal number. Return on first invalid char
// (or end). Return zero if not valid.
// 
// len: max size of buffer (must be MAX_NUM_LEN or larger).
// 
// Scan is valid for 1 1.2 1,2 1'234.5 1x 1.2x 1% 1.2% etc.
//
const REBYTE *Scan_Dec_Buf(const REBYTE *cp, REBCNT len, REBYTE *buf)
{
    REBYTE *bp = buf;
    REBYTE *be = bp + len - 1;
    REBOOL dig = FALSE;   /* flag that a digit was present */

    if (*cp == '+' || *cp == '-') *bp++ = *cp++;
    while (IS_LEX_NUMBER(*cp) || *cp == '\'')
        if (*cp != '\'') {
            *bp++ = *cp++;
            if (bp >= be) return 0;
            dig = TRUE;
        }
        else cp++;
    if (*cp == ',' || *cp == '.') cp++;
    *bp++ = '.';
    if (bp >= be) return 0;
    while (IS_LEX_NUMBER(*cp) || *cp == '\'')
        if (*cp != '\'') {
            *bp++ = *cp++;
            if (bp >= be) return 0;
            dig = TRUE;
        }
        else cp++;
    if (!dig) return 0;
    if (*cp == 'E' || *cp == 'e') {
            *bp++ = *cp++;
            if (bp >= be) return 0;
            dig = FALSE;
            if (*cp == '-' || *cp == '+') {
                *bp++ = *cp++;
                if (bp >= be) return 0;
            }
            while (IS_LEX_NUMBER(*cp)) {
                *bp++ = *cp++;
                if (bp >= be) return 0;
                dig = TRUE;
            }
            if (!dig) return 0;
    }
    *bp = 0;
    return cp;
}


//
//  Scan_Decimal: C
// 
// Scan and convert a decimal value.  Return zero if error.
//
const REBYTE *Scan_Decimal(REBDEC *out, const REBYTE *cp, REBCNT len, REBOOL dec_only)
{
    const REBYTE *bp = cp;
    REBYTE buf[MAX_NUM_LEN+4];
    REBYTE *ep = buf;
    REBOOL dig = FALSE;   /* flag that a digit was present */
    const char *se;

    if (len > MAX_NUM_LEN) return 0;

    if (*cp == '+' || *cp == '-') *ep++ = *cp++;
    while (IS_LEX_NUMBER(*cp) || *cp == '\'')
        if (*cp != '\'') {
            *ep++ = *cp++;
            dig = TRUE;
        }
        else cp++;
    if (*cp == ',' || *cp == '.') cp++;
    *ep++ = '.';
    while (IS_LEX_NUMBER(*cp) || *cp == '\'')
        if (*cp != '\'') {
            *ep++ = *cp++;
            dig = TRUE;
        }
        else cp++;
    if (!dig) return 0;
    if (*cp == 'E' || *cp == 'e') {
            *ep++ = *cp++;
            dig = FALSE;
            if (*cp == '-' || *cp == '+')
                *ep++ = *cp++;
            while (IS_LEX_NUMBER(*cp)) {
                *ep++ = *cp++;
                dig= TRUE;
            }
            if (!dig) return 0;
    }
    if (*cp == '%') {
        if (dec_only) return 0;
        cp++; // ignore it
    }
    *ep = 0;

    if ((REBCNT)(cp-bp) != len) return 0;

    // !!! need check for NaN, and INF
    *out = STRTOD(s_cast(buf), &se);

    if (fabs(*out) == HUGE_VAL) fail (Error(RE_OVERFLOW));
    return cp;
}


//
//  Scan_Integer: C
// 
// Scan and convert an integer value.  Return zero if error.
// Allow preceding + - and any combination of ' marks.
//
const REBYTE *Scan_Integer(REBI64 *out, const REBYTE *cp, REBCNT len)
{
    REBINT num = (REBINT)len;
    REBYTE buf[MAX_NUM_LEN+4];
    REBYTE *bp;
    REBOOL neg = FALSE;

    // Super-fast conversion of zero and one (most common cases):
    if (num == 1) {
        if (*cp == '0') {*out = 0; return cp + 1;}
        if (*cp == '1') {*out = 1; return cp + 1;}
    }

    if (len > MAX_NUM_LEN) return NULL; // prevent buffer overflow

    bp = buf;

    // Strip leading signs:
    if (*cp == '-') *bp++ = *cp++, num--, neg = TRUE;
    else if (*cp == '+') cp++, num--;

    // Remove leading zeros:
    for (; num > 0; num--) {
        if (*cp == '0' || *cp == '\'') cp++;
        else break;
    }

    // Copy all digits, except ' :
    for (; num > 0; num--) {
        if (*cp >= '0' && *cp <= '9') *bp++ = *cp++;
        else if (*cp == '\'') cp++;
        else return NULL;
    }
    *bp = 0;

    // Too many digits?
    len = bp - &buf[0];
    if (neg) len--;
    if (len > 19) {
        // !!! magic number :-( How does it relate to MAX_INT_LEN (also magic)
        return NULL;
    }

    // Convert, check, and return:
    errno = 0;
    *out = CHR_TO_INT(buf);
    if (errno != 0) return NULL; //overflow
    if ((*out > 0 && neg) || (*out < 0 && !neg)) return NULL;
    return cp;
}


//
//  Scan_Money: C
// 
// Scan and convert money.  Return zero if error.
//
const REBYTE *Scan_Money(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    const REBYTE *end;

    if (*cp == '$') cp++, len--;
    if (len == 0) return 0;
    SET_MONEY(value, string_to_deci(cp, &end));
    if (end != cp + len) return 0;

    return end;

#ifdef ndef
    REBYTE *bp = cp;
    REBYTE buf[MAX_NUM_LEN+8];
    REBYTE *ep = buf;
    REBCNT n = 0;
    REBOOL dig = FALSE;

    if (*cp == '+') cp++;
    else if (*cp == '-') *ep++ = *cp++;

    if (*cp != '$') {
        for (; Upper_Case[*cp] >= 'A' && Upper_Case[*cp] <= 'Z' && n < 3; cp++, n++) {
            VAL_MONEY_DENOM(value)[n] = Upper_Case[*cp];
        }
        if (*cp != '$' || n > 3) return 0;
        VAL_MONEY_DENOM(value)[n] = 0;
    } else VAL_MONEY_DENOM(value)[0] = 0;
    cp++;

    while (ep < buf+MAX_NUM_LEN && (IS_LEX_NUMBER(*cp) || *cp == '\''))
        if (*cp != '\'') *ep++ = *cp++, dig=1;
        else cp++;
    if (*cp == ',' || *cp == '.') cp++;
    *ep++ = '.';
    while (ep < buf+MAX_NUM_LEN && (IS_LEX_NUMBER(*cp) || *cp == '\''))
        if (*cp != '\'') *ep++ = *cp++, dig=1;
        else cp++;
    if (!dig) return 0;
    if (ep >= buf+MAX_NUM_LEN) return 0;
    *ep = 0;

    if ((REBCNT)(cp-bp) != len) return 0;
    VAL_RESET_HEADER(value, REB_MONEY);
    VAL_MONEY_AMOUNT(value) = atof((char*)(&buf[0]));
    if (fabs(VAL_MONEY_AMOUNT(value)) == HUGE_VAL) fail (Error(RE_OVERFLOW));
    return cp;
#endif
}


//
//  Scan_Date: C
// 
// Scan and convert a date. Also can include a time and zone.
//
const REBYTE *Scan_Date(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    const REBYTE *ep;
    const REBYTE *end = cp + len;
    REBINT num;
    REBINT day;
    REBINT month;
    REBINT year;
    REBINT tz = 0;
    REBYTE sep;
    REBCNT size;

    // Skip spaces:
    for (; *cp == ' ' && cp != end; cp++);

    // Skip day name, comma, and spaces:
    for (ep = cp; *ep != ',' && ep != end; ep++);
    if (ep != end) {
        cp = ep + 1;
        while (*cp == ' ' && cp != end) cp++;
    }
    if (cp == end) return 0;

    // Day or 4-digit year:
    ep = Grab_Int(cp, &num);
    if (num < 0) return 0;
    size = (REBCNT)(ep - cp);
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
        if (day == 0) return NULL;

        // !!! Clang static analyzer doesn't know from test of `day` below
        // how it connects with year being set or not.  Suppress warning.
        year = MIN_I32; // !!! Garbage, should not be read.
    }
    else return NULL;

    cp = ep;

    // Determine field separator:
    if (*cp != '/' && *cp != '-' && *cp != '.' && *cp != ' ') return 0;
    sep = *cp++;

    // Month as number or name:
    ep = Grab_Int(cp, &num);
    if (num < 0) return 0;
    size = (REBCNT)(ep - cp);
    if (size > 0) month = num;  // got a number
    else {      // must be a word
        for (ep = cp; IS_LEX_WORD(*ep); ep++); // scan word
        size = (REBCNT)(ep - cp);
        if (size < 3) return 0;
        for (num = 0; num < 12; num++) {
            if (!Compare_Bytes(cb_cast(Month_Names[num]), cp, size, TRUE)) break;
        }
        month = num + 1;
    }
    if (month < 1 || month > 12) return 0;
    cp = ep;
    if (*cp++ != sep) return 0;

    // Year or day (if year was first):
    ep = Grab_Int(cp, &num);
    if (*cp == '-' || num < 0) return 0;
    size = (REBCNT)(ep - cp);
    if (!size) return 0;

    if (day == 0) {
        // year already set, but day hasn't been
        day = num;
    }
    else {
        // day has been set, but year hasn't been

        // Allow shorthand form (e.g. /96) ranging +49,-51 years
        //      (so in year 2050 a 0 -> 2000 not 2100)
        if (size >= 3) year = num;
        else {
            year = (Current_Year / 100) * 100 + num;
            if (year - Current_Year > 50) year -=100;
            else if (year - Current_Year < -50) year += 100;
        }
    }
    if (year > MAX_YEAR || day < 1 || day > Month_Max_Days[month-1]) return 0;
    // Check February for leap year or century:
    if (month == 2 && day == 29) {
        if (((year % 4) != 0) ||        // not leap year
            ((year % 100) == 0 &&       // century?
            (year % 400) != 0)) return 0; // not leap century
    }

    cp = ep;
    VAL_TIME(value) = NO_TIME;
    if (cp >= end) goto end_date;

    if (*cp == '/' || *cp == ' ') {
        sep = *cp++;
        if (cp >= end) goto end_date;
        cp = Scan_Time(cp, 0, value);
        if (
            !cp
            || !IS_TIME(value)
            || (VAL_TIME(value) < 0)
            || (VAL_TIME(value) >= TIME_SEC(24 * 60 * 60))
        ){
            return NULL;
        }
    }

    if (*cp == sep) cp++;

    // Time zone can be 12:30 or 1230 (optional hour indicator)
    if (*cp == '-' || *cp == '+') {
        if (cp >= end) goto end_date;
        ep = Grab_Int(cp+1, &num);
        if (ep-cp == 0) return 0;
        if (*ep != ':') {
            int h, m;
            if (num < -1500 || num > 1500) return 0;
            h = (num / 100);
            m = (num - (h * 100));
            tz = (h * 60 + m) / ZONE_MINS;
        } else {
            if (num < -15 || num > 15) return 0;
            tz = num * (60/ZONE_MINS);
            if (*ep == ':') {
                ep = Grab_Int(ep+1, &num);
                if (num % ZONE_MINS != 0) return 0;
                tz += num / ZONE_MINS;
            }
        }
        if (ep != end) return 0;
        if (*cp == '-') tz = -tz;
        cp = ep;
    }
end_date:
    Set_Date_UTC(value, year, month, day, VAL_TIME(value), tz);
    return cp;
}


//
//  Scan_File: C
// 
// Scan and convert a file name.
//
const REBYTE *Scan_File(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    REBUNI term = 0;
    const REBYTE *invalid = cb_cast(":;()[]\"");
    REB_MOLD mo;
    CLEARS(&mo);

    if (*cp == '%') cp++, len--;
    if (*cp == '"') {
        cp++;
        len--;
        term = '"';
        invalid = cb_cast(":;\"");
    }
    cp = Scan_Item_Push_Mold(&mo, cp, cp+len, term, invalid);
    if (cp)
        Val_Init_File(value, Pop_Molded_String(&mo));
    else
        Drop_Mold(&mo);

    return cp;
}


//
//  Scan_Email: C
// 
// Scan and convert email.
//
const REBYTE *Scan_Email(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    REBYTE *str;
    REBOOL at = FALSE;
    REBUNI n;

    REBSER *series = Make_Binary(len);

    str = BIN_HEAD(series);
    for (; len > 0; len--) {
        if (*cp == '@') {
            if (at) return NULL;
            at = TRUE;
        }
        if (*cp == '%') {
            if (len <= 2 || !Scan_Hex2(cp+1, &n, FALSE)) return 0;
            *str++ = (REBYTE)n;
            cp += 3;
            len -= 2;
        }
        else *str++ = *cp++;
    }
    *str = 0;
    if (!at) return 0;
    SET_SERIES_LEN(series, cast(REBCNT, str - BIN_HEAD(series)));

    Val_Init_Series(value, REB_EMAIL, series); // manages

    return cp;
}


//
//  Scan_URL: C
// 
// Scan and convert a URL.
//
const REBYTE *Scan_URL(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    REBSER *series;
    REBYTE *str;
    REBUNI n;

//  !!! Need to check for any possible scheme followed by ':'

//  for (n = 0; n < URL_MAX; n++) {
//      if (str = Match_Bytes(cp, (REBYTE *)(URL_Schemes[n]))) break;
//  }
//  if (n >= URL_MAX) return 0;
//  if (*str != ':') return 0;

    series = Make_Binary(len);

    str = BIN_HEAD(series);
    for (; len > 0; len--) {
        //if (*cp == '%' && len > 2 && Scan_Hex2(cp+1, &n, FALSE)) {
        if (*cp == '%') {
            if (len <= 2 || !Scan_Hex2(cp+1, &n, FALSE)) return 0;
            *str++ = (REBYTE)n;
            cp += 3;
            len -= 2;
        }
        else *str++ = *cp++;
    }
    *str = 0;
    SET_SERIES_LEN(series, cast(REBCNT, str - BIN_HEAD(series)));

    Val_Init_Series(value, REB_URL, series); // manages
    return cp;
}


//
//  Scan_Pair: C
// 
// Scan and convert a pair
//
const REBYTE *Scan_Pair(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    REBYTE buf[MAX_NUM_LEN+4];
    const REBYTE *ep = Scan_Dec_Buf(cp, MAX_NUM_LEN, &buf[0]);
    const REBYTE *xp;

    if (!ep) return 0;
    VAL_PAIR_X(value) = (float)atof((char*)(&buf[0])); //n;
    if (*ep != 'x' && *ep != 'X') return 0;
    ep++;

    xp = Scan_Dec_Buf(ep, MAX_NUM_LEN, &buf[0]);
    if (!xp) return 0;
    VAL_PAIR_Y(value) = (float)atof((char*)(&buf[0])); //n;

    if (len > (REBCNT)(xp - cp)) return 0;
    VAL_RESET_HEADER(value, REB_PAIR);
    return xp;
}


//
//  Scan_Tuple: C
// 
// Scan and convert a tuple.
//
const REBYTE *Scan_Tuple(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    const REBYTE *ep;
    REBYTE *tp;
    REBCNT size = 1;
    REBINT n;

    if (len == 0) return 0;
    for (n = (REBINT)len, ep = cp; n > 0; n--, ep++)  // count '.'
        if (*ep == '.') size++;
    if (size > MAX_TUPLE) return 0;
    if (size < 3) size = 3;
    VAL_TUPLE_LEN(value) = (REBYTE)size;
    tp = VAL_TUPLE(value);
    memset(tp, 0, sizeof(REBTUP)-2);
    for (ep = cp; len > (REBCNT)(ep - cp); ep++) {
        ep = Grab_Int(ep, &n);
        if (n < 0 || n > 255) return 0;
        *tp++ = (REBYTE)n;
        if (*ep != '.') break;
    }
    if (len > (REBCNT)(ep - cp)) return 0;
    VAL_RESET_HEADER(value, REB_TUPLE);
    return ep;
}


//
//  Scan_Binary: C
// 
// Scan and convert binary strings.
//
const REBYTE *Scan_Binary(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    const REBYTE *ep;
    REBINT base = 16;

    if (*cp != '#') {
        ep = Grab_Int(cp, &base);
        if (cp == ep || *ep != '#') return 0;
        len -= (REBCNT)(ep - cp);
        cp = ep;
    }
    cp++;  // skip #
    if (*cp++ != '{') return 0;
    len -= 2;

    cp = Decode_Binary(value, cp, len, base, '}');
    if (!cp) return 0;

    cp = Skip_To_Byte(cp, cp + len, '}');
    if (!cp) return 0; // series will be gc'd

    return cp;
}


//
//  Scan_Any: C
// 
// Scan any string that does not require special decoding.
//
const REBYTE *Scan_Any(
    const REBYTE *cp,
    REBCNT len,
    REBVAL *value,
    enum Reb_Kind type
) {
    REBCNT n;

    // We hand it over to management by the GC, but don't run the GC before
    // the source has been scanned and put somewhere safe!
    //
    Val_Init_Series(value, type, Append_UTF8_May_Fail(0, cp, len));

    if (VAL_BYTE_SIZE(value)) {
        n = Deline_Bytes(VAL_BIN(value), VAL_LEN_AT(value));
    } else {
        n = Deline_Uni(VAL_UNI(value), VAL_LEN_AT(value));
    }

    SET_SERIES_LEN(VAL_SERIES(value), n);

    return cp + len;
}


//
//  scan-net-header: native [
//      {Scan an Internet-style header (HTTP, SMTP).}
//
//      header [string! binary!]
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
// The only reason it seemed to support BINARY! was to optimize the case
// where the binary only contained ASCII codepoints to dodge a string
// conversion.
//
// It doesn't make much sense to have this coded in C rather than using PARSE
// It's only being converted into a native to avoid introducing bugs by
// rewriting it as Rebol in the middle of other changes.
{
    PARAM(1, header);

    REBVAL *header = ARG(header);

    // Input variables from R3-Alpha REBNATIVE(construct)
    REBCNT index;
    REBARR *result;
    REBSER *temp;

    // Processing variables from R3-Alpha Scan_Net_Header()
    REBYTE *str;
    REBYTE *cp;
    REBYTE *start;
    REBVAL *val;
    REBINT len;
    REBARR *array;
    REBSER *string;

    result = Make_Array(10); // Just a guess at size (use STD_BUF?)
    Val_Init_Block(D_OUT, result); // Keep safe from GC

    // Convert string if necessary. Store back for GC safety.
    //
    temp = Temp_Bin_Str_Managed(header, &index, NULL);
    INIT_VAL_SERIES(header, temp); // caution: macro copies args!

    // !!! This is assuming the string is in bytes, but what if it was
    // unicode?  See R3-Alpha source for REBNATIVE(construct) for origin.
    //
    cp = VAL_BIN(header) + index;

    while (IS_LEX_ANY_SPACE(*cp)) cp++; // skip white space

    while (1) {
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

        if (*cp == ':') {
            REBSTR *name = Intern_UTF8_Managed(start, cp-start);
            RELVAL *item;
            cp++;
            // Search if word already present:
            for (item = ARR_HEAD(result); NOT_END(item); item += 2) {
                if (SAME_STR(VAL_WORD_CASED(item), name)) {
                    // Does it already use a block?
                    if (IS_BLOCK(item + 1)) {
                        // Block of values already exists:
                        val = Alloc_Tail_Array(VAL_ARRAY(val + 1));
                        SET_BLANK(val);
                    }
                    else {
                        // Create new block for values:
                        REBVAL *val2;
                        array = Make_Array(2);
                        val2 = Alloc_Tail_Array(array); // prior value
                        *val2 = val[1];
                        Val_Init_Block(val + 1, array);
                        val = Alloc_Tail_Array(array); // for new value
                        SET_BLANK(val);
                    }
                    break;
                }
            }
            if (IS_END(item)) {
                val = Alloc_Tail_Array(result); // add new word
                Val_Init_Word(val, REB_SET_WORD, name);
                val = Alloc_Tail_Array(result); // for new value
                SET_BLANK(val);
            }
        }
        else break;

        // Get value:
        while (IS_LEX_SPACE(*cp)) cp++;
        start = cp;
        len = 0;
        while (!ANY_CR_LF_END(*cp)) {
            len++;
            cp++;
        }
        // Is it continued on next line?
        while (*cp) {
            if (*cp == CR) cp++;
            if (*cp == LF) cp++;
            if (IS_LEX_SPACE(*cp)) {
                while (IS_LEX_SPACE(*cp)) cp++;
                while (!ANY_CR_LF_END(*cp)) {
                    len++;
                    cp++;
                }
            }
            else break;
        }

        // Create string value (ignoring lines and indents):
        string = Make_Binary(len);
        SET_SERIES_LEN(string, len);
        str = BIN_HEAD(string);
        cp = start;
        // Code below *MUST* mirror that above:
        while (!ANY_CR_LF_END(*cp)) *str++ = *cp++;
        while (*cp) {
            if (*cp == CR) cp++;
            if (*cp == LF) cp++;
            if (IS_LEX_SPACE(*cp)) {
                while (IS_LEX_SPACE(*cp)) cp++;
                while (!ANY_CR_LF_END(*cp))
                    *str++ = *cp++;
            }
            else break;
        }
        *str = 0;
        Val_Init_String(val, string);
    }

    return R_OUT;
}
