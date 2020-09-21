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
//  CT_Tuple: C
//
REBINT CT_Tuple(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);

    REBLEN len = MAX(VAL_TUPLE_LEN(a), VAL_TUPLE_LEN(b));
    assert(len < MAX_TUPLE);

    const REBYTE *vp1 = VAL_TUPLE(a);
    const REBYTE *vp2 = VAL_TUPLE(b);

    // Note: unused bytes in tuples are 0, so that 1.0.0 can = 1.0.0.0

    REBINT n;
    for (; len > 0; len--, ++vp1, ++vp2) {
        n = cast(REBINT, *vp1) - *vp2;
        if (n != 0)
            return n > 0 ? 1 : -1;
    }
    return 0;
}



//
//  MAKE_Tuple: C
//
REB_R MAKE_Tuple(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_TUPLE);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_TUPLE(arg))
        return Move_Value(out, arg);

    REBYTE buf[MAX_TUPLE];
    REBYTE *vp = buf;

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
    if (IS_TEXT(arg) or IS_URL(arg)) {
        REBSIZ size;
        const REBYTE *bp
            = Analyze_String_For_Scan(&size, arg, MAX_SCAN_TUPLE);

        if (Scan_Tuple(out, bp, size) == nullptr)
            fail (arg);
        return out;
    }

    if (ANY_ARRAY(arg)) {
        REBLEN len = 0;
        REBINT n;

        const RELVAL *item = VAL_ARRAY_AT(arg);

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

        return Init_Tuple(out, buf, len);
    }

    REBLEN alen;

    if (IS_ISSUE(arg)) {
        const REBSTR *spelling = VAL_STRING(arg);
        const REBYTE *ap = STR_HEAD(spelling);
        size_t size = STR_SIZE(spelling); // UTF-8 len
        if (size & 1)
            fail (arg); // must have even # of chars
        size /= 2;
        if (size > MAX_TUPLE)
            fail (arg); // valid even for UTF-8
        for (alen = 0; alen < size; alen++) {
            REBYTE decoded;
            if ((ap = Scan_Hex2(&decoded, ap)) == NULL)
                fail (arg);
            *vp++ = decoded;
        }
        Init_Tuple(out, buf, size);
    }
    else if (IS_BINARY(arg)) {
        REBLEN len = VAL_LEN_AT(arg);
        if (len > MAX_TUPLE)
            len = MAX_TUPLE;
        Init_Tuple(out, VAL_BIN_AT(arg), len);
        alen = len;
    }
    else
        fail (arg);

    for (; alen < MAX_TUPLE; alen++) *vp++ = 0;
    return out;

  bad_make:
    fail (Error_Bad_Make(REB_TUPLE, arg));
}


//
//  TO_Tuple: C
//
REB_R TO_Tuple(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Tuple(out, kind, nullptr, arg);
}


//
//  Pick_Tuple: C
//
void Pick_Tuple(REBVAL *out, const REBVAL *value, const RELVAL *picker)
{
    const REBYTE *dat = VAL_TUPLE(value);

    REBINT len = VAL_TUPLE_LEN(value);
    if (len < 3)
        len = 3;

    REBINT n = Get_Num_From_Arg(picker);

    // This uses modulus to avoid having a conditional access into the array,
    // which would trigger Spectre mitigation:
    //
    // https://stackoverflow.com/questions/50399940/
    //
    // By always accessing the array and always being in bounds, there's no
    // speculative execution accessing unbound locations.
    //
    REBYTE byte = dat[(n - 1) % len];
    if (n > 0 and n <= len)
        Init_Integer(out, byte);
    else
        Init_Nulled(out);
}


//
//  PD_Tuple: C
//
REB_R PD_Tuple(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    if (opt_setval)
        fail ("TUPLE!s are immutable, convert to BLOCK! and back to mutate");

    Pick_Tuple(pvs->out, pvs->out, picker);
    return pvs->out;
}


//
//  MF_Tuple: C
//
void MF_Tuple(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    // "Buffer must be large enough to hold longest tuple.
    //  Longest is: (3 digits + '.') * 11 nums + 1 term => 45"
    //
    // !!! ^-- Out of date comments; TUPLE! needs review and replacement.
    //
    REBYTE buf[60];

    REBLEN len = VAL_TUPLE_LEN(v);
    const REBYTE *tp = cast(const REBYTE *, VAL_TUPLE(v));

    REBYTE *out = buf;

    for (; len > 0; len--, tp++) {
        out = Form_Int(out, *tp);
        *out++ = '.';
    }

    len = VAL_TUPLE_LEN(v);
    while (len++ < 3) {
        *out++ = '0';
        *out++ = '.';
    }
    *--out = 0;

    Append_Ascii_Len(mo->series, s_cast(buf), out - buf);
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
    const REBYTE *ap;
    REBLEN alen;
    REBINT  a;
    REBDEC  dec;

    REBYTE buf[MAX_TUPLE];
    REBLEN len = VAL_TUPLE_LEN(value);
    memcpy(buf, VAL_TUPLE(value), len);
    REBYTE *vp = buf;

    REBSYM sym = VAL_WORD_SYM(verb);

    // !!! This used to depend on "IS_BINARY_ACT", a concept that does not
    // exist any longer with symbol-based action dispatch.  Patch with more
    // elegant mechanism.
    //
    if (
        sym == SYM_ADD
        or sym == SYM_SUBTRACT
        or sym == SYM_MULTIPLY
        or sym == SYM_DIVIDE
        or sym == SYM_REMAINDER
        or sym == SYM_INTERSECT
        or sym == SYM_UNION
        or sym == SYM_DIFFERENCE
    ){
        assert(vp);

        REBVAL *arg = D_ARG(2);

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
                len = alen;
            a = 646699; // unused but avoid maybe uninitialized warning
        }
        else
            fail (Error_Math_Args(REB_TUPLE, verb));

        REBLEN temp = len;
        for (; temp > 0; --temp, ++vp) {
            REBINT v = *vp;
            if (ap)
                a = (REBINT) *ap++;

            switch (VAL_WORD_SYM(verb)) {
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

                    // !!! After moving all the ROUND service routines to
                    // talk directly to ROUND frames, cases like this that
                    // don't have round frames need one.  Can't run:
                    //
                    //    v = cast(REBINT, Round_Dec(v / dec, 0, 1.0));
                    //
                    // The easiest way to do it is to call ROUND.  Methods for
                    // this are being improved all the time, so the slowness
                    // of scanning and binding is not too important.  (The
                    // TUPLE! code is all going to be replaced... so just
                    // consider this an API test.)
                    //
                    v = rebUnboxInteger(
                        "to integer! round divide", rebI(v), arg,
                    rebEND);
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

            case SYM_INTERSECT:
                v &= a;
                break;

            case SYM_UNION:
                v |= a;
                break;

            case SYM_DIFFERENCE:
                v ^= a;
                break;

            default:
                return R_UNHANDLED;
            }

            if (v > 255)
                v = 255;
            else if (v < 0)
                v = 0;
            *vp = cast(REBYTE, v);
        }
        return Init_Tuple(D_OUT, buf, len);
    }

    // !!!! merge with SWITCH below !!!
    if (sym == SYM_COMPLEMENT) {
        REBLEN temp = len;
        for (; temp > 0; --temp, vp++)
            *vp = cast(REBYTE, ~*vp);
        return Init_Tuple(D_OUT, buf, len);
    }
    if (sym == SYM_RANDOM) {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());
        for (; len > 0; len--, vp++) {
            if (*vp)
                *vp = cast(REBYTE, Random_Int(did REF(secure)) % (1 + *vp));
        }
        return Init_Tuple(D_OUT, buf, len);
    }

    switch (sym) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            return Init_Integer(D_OUT, MAX(len, 3));

        default:
            break;
        }

        break; }

      case SYM_COPY:
        RETURN (value);

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;

        UNUSED(PAR(series));

        REBLEN temp = len;

        if (REF(part)) {
            REBLEN part = Get_Num_From_Arg(ARG(part));
            temp = MIN(part, VAL_TUPLE_LEN(value));
        }
        if (len > 0) {
            REBLEN i;
            for (i = 0; i < temp/2; i++) {
                a = vp[temp - i - 1];
                vp[temp - i - 1] = vp[i];
                vp[i] = a;
            }
        }
        return Init_Tuple(D_OUT, buf, len); }
/*
  poke_it:
        a = Get_Num_From_Arg(arg);
        if (a <= 0 || a > len) {
            if (action == A_PICK) return nullptr;
            fail (Error_Out_Of_Range(arg));
        }
        if (action == A_PICK)
            return Init_Integer(D_OUT, vp[a-1]);
        // Poke:
        if (not IS_INTEGER(D_ARG(3)))
            fail (D_ARG(3));
        v = VAL_INT32(D_ARG(3));
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
        vp[a-1] = v;
        RETURN (value);
*/

      default:
        break;
    }

    return R_UNHANDLED;
}
