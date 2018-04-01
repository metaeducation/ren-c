//
//  File: %f-math.c
//  Summary: "basic math conversions"
//  Section: functional
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
// Do not underestimate what it takes to make some parts of this
// portable over all systems. Modifications to this code should be
// tested on multiple operating system runtime libraries, including
// older/obsolete systems.
//

#include "sys-core.h"
#include "sys-dec-to-char.h"


//
//  Grab_Int: C
//
// Grab an integer value from the string.
//
// Return the character position just after the integer and return
// the integer via a pointer to it.
//
// Notes:
//     1. Stops at the first non-digit.
//     2. If no integer found, pointer doesn't change position.
//     3. Integers may contain REBOL tick (') marks.
//
const REBYTE *Grab_Int(const REBYTE *cp, REBINT *val)
{
    REBINT value = 0;
    REBOOL neg = FALSE;

    if (*cp == '-') cp++, neg = TRUE;
    else if (*cp == '+') cp++;

    while (*cp >= '0' && *cp <= '9') {
        value = (value * 10) + (*cp - '0');
        cp++;
    }

    *val = neg ? -value : value;

    return cp;
}


//
//  Grab_Int_Scale: C
//
// Return integer scaled to the number of digits specified.
// Used for the decimal part of numbers (e.g. times).
//
const REBYTE *Grab_Int_Scale(const REBYTE *cp, REBINT *val, REBCNT scale)
{
    REBI64 value = 0;

    for (;scale > 0 && *cp >= '0' && *cp <= '9'; scale--) {
        value = (value * 10) + (*cp - '0');
        cp++;
    }

    // Round up if necessary:
    if (*cp >= '5' && *cp <= '9') value++;

    // Ignore excess digits:
    while (*cp >= '0' && *cp <= '9') cp++;

    // Make sure its full scale:
    for (;scale > 0; scale--) value *= 10;

    *val = (REBINT)value;
    return cp;
}


//
//  Form_Int_Len: C
//
// Form an integer string into the given buffer. Result will
// not exceed maxl length, including terminator.
//
// Returns the length of the string.
//
// Notes:
//     1. If result is longer than maxl, returns 0 length.
//     2. Make sure you have room in your buffer!
//
REBINT Form_Int_Len(REBYTE *buf, REBI64 val, REBINT maxl)
{
    REBYTE tmp[MAX_NUM_LEN];
    REBYTE *tp = tmp;
    REBI64 n;
    REBI64 r;
    REBINT len = 0;

    // defaults for problem cases
    buf[0] = '?';
    buf[1] = 0;

    if (maxl == 0) return 0;

    if (val == 0) {
        *buf++ = '0';
        *buf = 0;
        return 1;
    }

#define MIN_I64_STR "-9223372036854775808"
    if (val == INT64_MIN) {
        len = strlen(MIN_I64_STR);
        if (maxl < len + 1) return 0;
        memcpy(buf, MIN_I64_STR, len + 1);
        return len;
    }

    if (val < 0) {
        val = -val;
        *buf++ = '-';
        maxl--;
        len = 1;
    }

    // Generate string in reverse:
    *tp++ = 0;
    while (val != 0 && maxl > 0 && tp < tmp + MAX_NUM_LEN) {
        n = val / 10;   // not using ldiv for easier compatibility
        r = val % 10;
        *tp++ = (REBYTE)('0' + (REBYTE)(r));
        val = n;
        maxl --;
    }
    tp--;

    if (maxl == 0) {
        return 0;
    }

    while ((*buf++ = *tp--)) len++;
    return len;
}


//
//  Form_Int_Pad: C
//
// Form an integer string in the given buffer with a min
// width padded out with the given character. Len > 0 left
// aligned. Len < 0 is right aligned.
//
// If len = 0 and val = 0, a null string is formed.
// Make sure you have room in your buffer before calling this!
//
REBYTE *Form_Int_Pad(REBYTE *buf, REBI64 val, REBINT max, REBINT len, REBYTE pad)
{
    REBYTE tmp[MAX_NUM_LEN];
    REBINT n;

    n = Form_Int_Len(tmp, val, max + 1);
    if (n == 0) {
        strcpy(s_cast(buf), "??");
        return buf;  // too long
    }

    if (len >= 0) {
        strcpy(s_cast(buf), s_cast(tmp));
        buf += n;
        for (; n < len; n++) *buf++ = pad;
    }
    else { // len < 0
        for (; n < -len; len++) *buf++ = pad;
        strcpy(s_cast(buf), s_cast(tmp));
        buf += n;
    }

    *buf = 0;
    return buf;
}


//
//  Form_Int: C
//
// Form 32 bit integer string in the given buffer.
// Make sure you have room in your buffer before calling this!
//
REBYTE *Form_Int(REBYTE *buf, REBINT val)
{
    REBINT len = Form_Int_Len(buf, val, MAX_NUM_LEN);
    return buf + len;
}


//
//  Form_Integer: C
//
// Form standard REBOL integer value (32 or 64).
// Make sure you have room in your buffer before calling this!
//
REBYTE *Form_Integer(REBYTE *buf, REBI64 val)
{
    INT_TO_STR(val, buf);
    return buf+LEN_BYTES(buf);
}


//
//  Emit_Integer: C
//
REBINT Emit_Integer(REBYTE *buf, REBI64 val)
{
    INT_TO_STR(val, buf);
    return LEN_BYTES(buf);
}



#define MIN_DIGITS 1
/* this is appropriate for 64-bit IEEE754 binary floating point format */
#define MAX_DIGITS 17

//
//  Emit_Decimal: C
//
REBINT Emit_Decimal(
    REBYTE *cp,
    REBDEC d,
    REBFLGS flags, // DEC_MOLD_PERCENT, DEC_MOLD_MINIMAL
    REBYTE point,
    REBINT decimal_digits
) {
    REBYTE *start = cp, *sig, *rve;
    int e, sgn;
    REBINT digits_obtained;

    /* sanity checks */
    if (decimal_digits < MIN_DIGITS) decimal_digits = MIN_DIGITS;
    else if (decimal_digits > MAX_DIGITS) decimal_digits = MAX_DIGITS;

    sig = (REBYTE *) dtoa (d, 0, decimal_digits, &e, &sgn, (char **) &rve);

    digits_obtained = rve - sig;

    /* handle sign */
    if (sgn) *cp++ = '-';

    if (flags & DEC_MOLD_PERCENT) e += 2;

    if ((e > decimal_digits) || (e <= -6)) {
        /* e-format */
        *cp++ = *sig++;

        /* insert the radix point */
        *cp++ = point;

        /* insert the rest */
        memcpy(cp, sig, digits_obtained - 1);
        cp += digits_obtained - 1;
    } else if (e > 0) {
        if (e <= digits_obtained) {
            /* insert digits preceding point */
            memcpy (cp, sig, e);
            cp += e;
            sig += e;

            *cp++ = point;

            /* insert digits following point */
            memcpy(cp, sig, digits_obtained -  e);
            cp += digits_obtained - e;
        } else {
            /* insert all digits obtained */
            memcpy (cp, sig, digits_obtained);
            cp += digits_obtained;

            /* insert zeros preceding point */
            memset (cp, '0', e - digits_obtained);
            cp += e - digits_obtained;

            *cp++ = point;
        }
        e = 0;
    } else {
        *cp++ = '0';

        *cp++ = point;

        memset(cp, '0', -e);
        cp -= e;

        memcpy(cp, sig, digits_obtained);
        cp += digits_obtained;

        e = 0;
    }

    // Add at least one zero after point (unless percent or pair):
    if (*(cp - 1) == point) {
        if ((flags & DEC_MOLD_PERCENT) || (flags & DEC_MOLD_MINIMAL))
            cp--;
        else
            *cp++ = '0';
    }

    // Add E part if needed:
    if (e) {
        *cp++ = 'e';
        INT_TO_STR(e - 1, cp);
        cp = b_cast(strchr(s_cast(cp), 0));
    }

    if (flags & DEC_MOLD_PERCENT) *cp++ = '%';
    *cp = 0;
    return cp - start;
}
