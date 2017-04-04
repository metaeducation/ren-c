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
void MAKE_Tuple(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_TUPLE);
    UNUSED(kind);

    if (IS_TUPLE(arg)) {
        Move_Value(out, arg);
        return;
    }

    Reset_Val_Header(out, REB_TUPLE);
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
        if (Scan_Tuple(out, ap, len) != NULL)
            return;
        fail (arg);
    }

    if (ANY_ARRAY(arg)) {
        REBCNT len = 0;
        REBINT n;

        RELVAL *item = VAL_ARRAY_AT(arg);

        for (; NOT_END(item); ++item, ++vp, ++len) {
            if (len >= MAX_TUPLE)
                goto bad_make;
            if (IS_INTEGER(item)) {
                n = Int32(item);
            }
            else if (IS_CHAR(item)) {
                n = VAL_CHAR(item);
            }
            else
                goto bad_make;

            if (n > 255 || n < 0)
                goto bad_make;
            *vp = n;
        }

        VAL_TUPLE_LEN(out) = len;

        for (; len < MAX_TUPLE; len++) *vp++ = 0;
        return;
    }

    REBCNT alen;

    if (IS_ISSUE(arg)) {
        REBUNI c;
        const REBYTE *ap = VAL_WORD_HEAD(arg);
        REBCNT len = LEN_BYTES(ap);  // UTF-8 len
        if (len & 1)
            fail (arg); // must have even # of chars
        len /= 2;
        if (len > MAX_TUPLE)
            fail (arg); // valid even for UTF-8
        VAL_TUPLE_LEN(out) = len;
        for (alen = 0; alen < len; alen++) {
            const REBOOL unicode = FALSE;
            if (!Scan_Hex2(ap, &c, unicode))
                fail (arg);
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
    else
        fail (arg);

    for (; alen < MAX_TUPLE; alen++) *vp++ = 0;
    return;

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
//  Pick_Tuple: C
//
void Pick_Tuple(REBVAL *out, const REBVAL *value, const REBVAL *picker)
{
    const REBYTE *dat = VAL_TUPLE(value);

    REBINT len = VAL_TUPLE_LEN(value);
    if (len < 3)
        len = 3;

    REBINT n = Get_Num_From_Arg(picker);
    if (n > 0 && n <= len)
        Init_Integer(out, dat[n - 1]);
    else
        Init_Void(out);
}


//
//  Poke_Tuple_Immediate: C
//
// !!! Note: In the current implementation, tuples are immediate values.
// So a POKE only changes the `value` in your hand.
//
void Poke_Tuple_Immediate(
    REBVAL *value,
    const REBVAL *picker,
    const REBVAL *poke
) {
    REBYTE *dat = VAL_TUPLE(value);

    REBINT len = VAL_TUPLE_LEN(value);
    if (len < 3)
        len = 3;

    REBINT n = Get_Num_From_Arg(picker);
    if (n <= 0 || n > cast(REBINT, MAX_TUPLE))
        fail (Error_Out_Of_Range(picker));

    REBINT i;
    if (IS_INTEGER(poke) || IS_DECIMAL(poke))
        i = Int32(poke);
    else if (IS_BLANK(poke)) {
        n--;
        CLEAR(dat + n, MAX_TUPLE - n);
        VAL_TUPLE_LEN(value) = n;
        return;
    }
    else
        fail (poke);

    if (i < 0)
        i = 0;
    else if (i > 255)
        i = 255;

    dat[n - 1] = i;
    if (n > len)
        VAL_TUPLE_LEN(value) = n;
}


//
//  PD_Tuple: C
//
REBINT PD_Tuple(REBPVS *pvs)
{
    if (pvs->opt_setval) {
        //
        // !!! Is this a good idea?  It means `x: 10.10.10 | y: (x/2: 20)` does
        // result in y being 10.20.10, but x is unchanged.
        //
        Poke_Tuple_Immediate(
            KNOWN(pvs->value), pvs->selector, pvs->opt_setval
        );
        return PE_OK;
    }

    Pick_Tuple(pvs->store, KNOWN(pvs->value), pvs->selector);
    return PE_USE_STORE;
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
// !!! The TUPLE type from Rebol is something of an oddity, plus written as
// more-or-less spaghetti code.  It is likely to be replaced with something
// generalized better, but is grudgingly kept working in the meantime.
//
REBTYPE(Tuple)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    const REBYTE *ap;
    REBCNT len;
    REBCNT alen;
    REBINT  a;
    REBDEC  dec;

    assert(IS_TUPLE(value));

    REBYTE *vp = VAL_TUPLE(value);
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
            dec = -207.6382; // unused but avoid maybe uninitialized warning
            a = VAL_INT32(arg);
            ap = 0;
        }
        else if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
            dec = VAL_DECIMAL(arg);
            a = cast(REBINT, dec);
            ap = 0;
        }
        else if (IS_TUPLE(arg)) {
            dec = -251.8517; // unused but avoid maybe uninitialized warning
            ap = VAL_TUPLE(arg);
            alen = VAL_TUPLE_LEN(arg);
            if (len < alen)
                len = VAL_TUPLE_LEN(value) = alen;
            a = 646699; // unused but avoid maybe uninitialized warning
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
                    v = cast(REBINT, v * dec);
                else
                    v *= a;
                break;

            case SYM_DIVIDE:
                if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
                    if (dec == 0.0)
                        fail (Error_Zero_Divide_Raw());

                    v = cast(REBINT, Round_Dec(v / dec, 0, 1.0));
                }
                else {
                    if (a == 0)
                        fail (Error_Zero_Divide_Raw());
                    v /= a;
                }
                break;

            case SYM_REMAINDER:
                if (a == 0)
                    fail (Error_Zero_Divide_Raw());
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
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());
        for (; len > 0; len--, vp++) {
            if (*vp)
                *vp = cast(REBYTE, Random_Int(REF(secure)) % (1 + *vp));
        }
        goto ret_value;
    }

    switch (action) {
    case SYM_LENGTH:
        len = MAX(len, 3);
        Init_Integer(D_OUT, len);
        return R_OUT;

    case SYM_PICK_P:
        Pick_Tuple(D_OUT, value, arg);
        return R_OUT;

    // !!! TUPLE! is an immediate value at the current time, and does not
    // support POKE.  But see PD_Tuple for notes on its behavior with SET-PATH!
    //
    /*case SYM_POKE:
        Poke_Tuple_Immediate(value, arg, D_ARG(3));
        Move_Value(D_OUT, D_ARG(3));
        return R_OUT;*/

    case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;

        UNUSED(PAR(series));

        if (REF(part)) {
            len = Get_Num_From_Arg(ARG(limit));
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
        goto ret_value; }
/*
  poke_it:
        a = Get_Num_From_Arg(arg);
        if (a <= 0 || a > len) {
            if (action == A_PICK) return R_BLANK;
            fail (Error_Out_Of_Range(arg));
        }
        if (action == A_PICK) {
            Init_Integer(D_OUT, vp[a-1]);
            return R_OUT;
        }
        // Poke:
        if (NOT(IS_INTEGER(D_ARG(3))))
            fail (D_ARG(3));
        v = VAL_INT32(D_ARG(3));
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
        vp[a-1] = v;
        goto ret_value;

*/
        fail (Error_Bad_Make(REB_TUPLE, arg));

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_TUPLE, action));

ret_value:
    Move_Value(D_OUT, value);
    return R_OUT;
}
