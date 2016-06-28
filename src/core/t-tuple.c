//
//  File: %t-tuple.c
//  Summary: "tuple datatype"
//  Section: datatypes
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


//
//  CT_Tuple: C
//
REBINT CT_Tuple(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBINT num = Cmp_Tuple(a, b);
    if (mode > 1) return (num == 0 && VAL_TUPLE_LEN(a) == VAL_TUPLE_LEN(b));
    if (mode >= 0)  return (num == 0);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}



//
//  MAKE_Tuple: C
//
void MAKE_Tuple(REBVAL *out, enum Reb_Kind type, const REBVAL *arg)
{
    if (IS_TUPLE(arg)) {
        *out = *arg;
        return;
    }

    VAL_RESET_HEADER(out, REB_TUPLE);
    REBYTE *vp = VAL_TUPLE(out);

    // !!! Net lookup parses IP addresses out of `tcp://93.184.216.34` or
    // similar URL!s.  In Rebol3 these captures come back the same type
    // as the input instead of as STRING!, which was a latent bug in the
    // network code of the 12-Dec-2012 release:
    //
    // https://github.com/rebol/rebol/blob/master/src/mezz/sys-ports.r#L110
    //
    // All attempts to convert a URL!-flavored IP address failed.  Taking
    // URL! here fixes it, though there are still open questions.
    //
    if (IS_STRING(arg) || IS_URL(arg)) {
        REBCNT len;
        REBYTE *ap = Temp_Byte_Chars_May_Fail(arg, MAX_SCAN_TUPLE, &len, FALSE);
        if (Scan_Tuple(ap, len, out))
            return;
        goto bad_arg;
    }

    if (ANY_ARRAY(arg)) {
        REBINT len = 0;
        REBINT n;

        RELVAL *item = VAL_ARRAY_AT(arg);

        for (; NOT_END(item); ++item, ++vp, ++len) {
            if (len >= 10) goto bad_make;
            if (IS_INTEGER(item)) {
                n = Int32(item);
            }
            else if (IS_CHAR(item)) {
                n = VAL_CHAR(item);
            }
            else
                goto bad_make;

            if (n > 255 || n < 0) goto bad_make;
            *vp = n;
        }

        VAL_TUPLE_LEN(out) = len;

        for (; len < 10; len++) *vp++ = 0;
        return;
    }

    REBCNT alen;

    if (IS_ISSUE(arg)) {
        REBUNI c;
        const REBYTE *ap = STR_HEAD(VAL_WORD_CASED(arg));
        REBCNT len = LEN_BYTES(ap);  // UTF-8 len
        if (len & 1) goto bad_arg; // must have even # of chars
        len /= 2;
        if (len > MAX_TUPLE) goto bad_arg; // valid even for UTF-8
        VAL_TUPLE_LEN(out) = len;
        for (alen = 0; alen < len; alen++) {
            const REBOOL unicode = FALSE;
            if (!Scan_Hex2(ap, &c, unicode)) goto bad_arg;
            *vp++ = cast(REBYTE, c);
            ap += 2;
        }
    }
    else if (IS_BINARY(arg)) {
        REBYTE *ap = VAL_BIN_AT(arg);
        REBCNT len = VAL_LEN_AT(arg);
        if (len > MAX_TUPLE) len = MAX_TUPLE;
        VAL_TUPLE_LEN(out) = len;
        for (alen = 0; alen < len; alen++) *vp++ = *ap++;
    }
    else goto bad_arg;

    for (; alen < MAX_TUPLE; alen++) *vp++ = 0;
    return;

bad_arg:
    fail (Error_Invalid_Arg(arg));

bad_make:
    fail (Error_Bad_Make(REB_TUPLE, arg));
}


//
//  TO_Tuple: C
//
void TO_Tuple(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Tuple(out, kind, arg);
}


//
//  Cmp_Tuple: C
// 
// Given two tuples, compare them.
//
REBINT Cmp_Tuple(const RELVAL *t1, const RELVAL *t2)
{
    REBCNT  len;
    const REBYTE *vp1, *vp2;
    REBINT  n;

    len = MAX(VAL_TUPLE_LEN(t1), VAL_TUPLE_LEN(t2));
    vp1 = VAL_TUPLE(t1);
    vp2 = VAL_TUPLE(t2);

    for (;len > 0; len--, vp1++,vp2++) {
        n = (REBINT)(*vp1 - *vp2);
        if (n != 0)
            return n;
    }
    return 0;
}


//
//  PD_Tuple: C
// 
// Implements PATH and SET_PATH for tuple.
// Sets DS_TOP if found. Always returns 0.
//
REBINT PD_Tuple(REBPVS *pvs)
{
    const REBVAL *setval;
    REBINT n;
    REBINT i;
    REBYTE *dat;
    REBINT len;

    dat = VAL_TUPLE(pvs->value);
    len = VAL_TUPLE_LEN(pvs->value);

    if (len < 3) { len = 3; }

    n = Get_Num_From_Arg(pvs->selector);

    if ((setval = pvs->opt_setval)) {
        if (n <= 0 || n > MAX_TUPLE)
            fail (Error_Bad_Path_Select(pvs));

        if (IS_INTEGER(setval) || IS_DECIMAL(setval))
            i = Int32(setval);
        else if (IS_BLANK(setval)) {
            n--;
            CLEAR(dat + n, MAX_TUPLE - n);
            VAL_TUPLE_LEN(pvs->value) = n;
            return PE_OK;
        }
        else
            fail (Error_Bad_Path_Set(pvs));

        if (i < 0) i = 0;
        else if (i > 255) i = 255;

        dat[n - 1] = i;
        if (n > len)
            VAL_TUPLE_LEN(pvs->value) = n;

        return PE_OK;
    }
    else {
        if (n > 0 && n <= len) {
            SET_INTEGER(pvs->store, dat[n - 1]);
            return PE_USE_STORE;
        }
        else return PE_NONE;
    }
}


//
//  Emit_Tuple: C
// 
// The out array must be large enough to hold longest tuple.
// Longest is: (3 digits + '.') * 11 nums + 1 term => 45
//
REBINT Emit_Tuple(const REBVAL *value, REBYTE *out)
{
    REBCNT len = VAL_TUPLE_LEN(value);
    const REBYTE *tp = cast(const REBYTE *, VAL_TUPLE(value));
    REBYTE *start = out;

    for (; len > 0; len--, tp++) {
        out = Form_Int(out, *tp);
        *out++ = '.';
    }

    len = VAL_TUPLE_LEN(value);
    while (len++ < 3) {
        *out++ = '0';
        *out++ = '.';
    }
    *--out = 0;

    return out-start;
}


//
//  REBTYPE: C
//
REBTYPE(Tuple)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBYTE  *vp;
    const REBYTE *ap;
    REBCNT len;
    REBCNT alen;
    REBINT  a;
    REBDEC  dec;

    assert(IS_TUPLE(value));
    vp = VAL_TUPLE(value);
    len = VAL_TUPLE_LEN(value);

    // !!! This used to depend on "IS_BINARY_ACT", a concept that does not
    // exist any longer with symbol-based action dispatch.  Patch with more
    // elegant mechanism.
    //
    if (
        action == SYM_ADD
        || action == SYM_SUBTRACT
        || action == SYM_MULTIPLY
        || action == SYM_DIVIDE
        || action == SYM_REMAINDER
        || action == SYM_AND_T
        || action == SYM_OR_T
        || action == SYM_XOR_T
    ){
        assert(vp);

        if (IS_INTEGER(arg)) {
            a = VAL_INT32(arg);
            ap = 0;
        } else if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
            dec=VAL_DECIMAL(arg);
            a = (REBINT)dec;
            ap = 0;
        } else if (IS_TUPLE(arg)) {
            ap = VAL_TUPLE(arg);
            alen = VAL_TUPLE_LEN(arg);
            if (len < alen)
                len = VAL_TUPLE_LEN(value) = alen;
        }
        else
            fail (Error_Math_Args(REB_TUPLE, action));

        for (;len > 0; len--, vp++) {
            REBINT v = *vp;
            if (ap)
                a = (REBINT) *ap++;

            switch (action) {
            case SYM_ADD: v += a; break;

            case SYM_SUBTRACT: v -= a; break;

            case SYM_MULTIPLY:
                if (IS_DECIMAL(arg) || IS_PERCENT(arg))
                    v=(REBINT)(v*dec);
                else
                    v *= a;
                break;

            case SYM_DIVIDE:
                if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
                    if (dec == 0.0) fail (Error(RE_ZERO_DIVIDE));
                    v=(REBINT)Round_Dec(v/dec, 0, 1.0);
                } else {
                    if (a == 0) fail (Error(RE_ZERO_DIVIDE));
                    v /= a;
                }
                break;

            case SYM_REMAINDER:
                if (a == 0) fail (Error(RE_ZERO_DIVIDE));
                v %= a;
                break;

            case SYM_AND_T:
                v &= a;
                break;

            case SYM_OR_T:
                v |= a;
                break;

            case SYM_XOR_T:
                v ^= a;
                break;

            default:
                fail (Error_Illegal_Action(REB_TUPLE, action));
            }

            if (v > 255) v = 255;
            else if (v < 0) v = 0;
            *vp = (REBYTE) v;
        }
        goto ret_value;
    }

    // !!!! merge with SWITCH below !!!
    if (action == SYM_COMPLEMENT) {
        for (;len > 0; len--, vp++)
            *vp = (REBYTE)~*vp;
        goto ret_value;
    }
    if (action == SYM_RANDOM) {
        if (D_REF(2)) fail (Error(RE_BAD_REFINES)); // seed
        for (;len > 0; len--, vp++) {
            if (*vp)
                *vp = (REBYTE)(Random_Int(D_REF(3)) % (1+*vp));
        }
        goto ret_value;
    }
/*
    if (action == A_ZEROQ) {
        for (;len > 0; len--, vp++) {
            if (*vp != 0)
                goto is_false;
        }
        goto is_true;
    }
*/
    //a = 1; //???
    switch (action) {
    case SYM_LENGTH:
        len = MAX(len, 3);
        SET_INTEGER(D_OUT, len);
        return R_OUT;

    case SYM_PICK:
        Pick_Path(D_OUT, value, arg, 0);
        return R_OUT;

/// case SYM_POKE:
///     Pick_Path(D_OUT, value, arg, D_ARG(3));
///     *D_OUT = *D_ARG(3);
///     return R_OUT;

    case SYM_REVERSE:
        if (D_REF(2)) {
            len = Get_Num_From_Arg(D_ARG(3));
            len = MIN(len, VAL_TUPLE_LEN(value));
        }
        if (len > 0) {
            REBCNT i;
            //len = MAX(len, 3);
            for (i = 0; i < len/2; i++) {
                a = vp[len - i - 1];
                vp[len - i - 1] = vp[i];
                vp[i] = a;
            }
        }
        goto ret_value;
/*
  poke_it:
        a = Get_Num_From_Arg(arg);
        if (a <= 0 || a > len) {
            if (action == A_PICK) return R_BLANK;
            fail (Error_Out_Of_Range(arg));
        }
        if (action == A_PICK) {
            SET_INTEGER(D_OUT, vp[a-1]);
            return R_OUT;
        }
        // Poke:
        if (!IS_INTEGER(D_ARG(3))) fail (Error_Invalid_Arg(D_ARG(3)));
        v = VAL_INT32(D_ARG(3));
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
        vp[a-1] = v;
        goto ret_value;

*/
        fail (Error_Bad_Make(REB_TUPLE, arg));
    }

    fail (Error_Illegal_Action(REB_TUPLE, action));

ret_value:
    *D_OUT = *value;
    return R_OUT;
}
