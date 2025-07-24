//
//  file: %t-tuple.c
//  summary: "tuple datatype"
//  section: datatypes
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
//  CT_Tuple: C
//
REBINT CT_Tuple(const Cell* a, const Cell* b, REBINT mode)
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
Bounce MAKE_Tuple(Value* out, Type type, const Value* arg)
{
    assert(type == TYPE_TUPLE);
    UNUSED(type);

    if (Is_Tuple(arg))
        return Copy_Cell(out, arg);

    RESET_CELL(out, TYPE_TUPLE);
    Byte *vp = VAL_TUPLE(out);

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
    if (Is_Text(arg) or Is_Url(arg)) {
        Size size;
        Byte *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_TUPLE);
        Erase_Cell(out);
        if (Scan_Tuple(out, bp, size) == nullptr)
            panic (Error_Invalid(arg));
        return out;
    }

    if (Any_List(arg)) {
        REBLEN len = 0;
        REBINT n;

        Cell* item = List_At(arg);

        for (; NOT_END(item); ++item, ++vp, ++len) {
            if (len >= MAX_TUPLE)
                goto bad_make;
            if (Is_Integer(item)) {
                n = Int32(item);
            }
            else if (Is_Char(item)) {
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
        return out;
    }

    REBLEN alen;

    if (Is_Issue(arg)) {
        Symbol* symbol = Word_Symbol(arg);
        const Byte *ap = b_cast(Symbol_Head(symbol));
        size_t size = Symbol_Size(symbol); // UTF-8 len
        if (size & 1)
            panic (arg); // must have even # of chars
        size /= 2;
        if (size > MAX_TUPLE)
            panic (arg); // valid even for UTF-8
        VAL_TUPLE_LEN(out) = size;
        for (alen = 0; alen < size; alen++) {
            const bool unicode = false;
            Ucs2Unit ch;
            if (!Scan_Hex2(&ch, ap, unicode))
                panic (Error_Invalid(arg));
            *vp++ = cast(Byte, ch);
            ap += 2;
        }
    }
    else if (Is_Blob(arg)) {
        Byte *ap = Blob_At(arg);
        REBLEN len = Series_Len_At(arg);
        if (len > MAX_TUPLE) len = MAX_TUPLE;
        VAL_TUPLE_LEN(out) = len;
        for (alen = 0; alen < len; alen++) *vp++ = *ap++;
    }
    else
        panic (Error_Invalid(arg));

    for (; alen < MAX_TUPLE; alen++) *vp++ = 0;
    return out;

  bad_make:
    panic (Error_Bad_Make(TYPE_TUPLE, arg));
}


//
//  TO_Tuple: C
//
Bounce TO_Tuple(Value* out, Type type, const Value* arg)
{
    return MAKE_Tuple(out, type, arg);
}


//
//  Cmp_Tuple: C
//
// Given two tuples, compare them.
//
REBINT Cmp_Tuple(const Cell* t1, const Cell* t2)
{
    REBLEN  len;
    const Byte *vp1, *vp2;
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
void Pick_Tuple(Value* out, const Value* value, const Value* picker)
{
    const Byte *dat = VAL_TUPLE(value);

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
    Byte byte = dat[(n - 1) % len];
    if (n > 0 and n <= len)
        Init_Integer(out, byte);
    else
        Init_Nulled(out);
}


//
//  Poke_Tuple_Immediate: C
//
// !!! Note: In the current implementation, tuples are immediate values.
// So a POKE only changes the `value` in your hand.
//
void Poke_Tuple_Immediate(
    Value* value,
    const Value* picker,
    const Value* poke
) {
    Byte *dat = VAL_TUPLE(value);

    REBINT len = VAL_TUPLE_LEN(value);
    if (len < 3)
        len = 3;

    REBINT n = Get_Num_From_Arg(picker);
    if (n <= 0 || n > cast(REBINT, MAX_TUPLE))
        panic (Error_Out_Of_Range(picker));

    REBINT i;
    if (Is_Integer(poke) || Is_Decimal(poke))
        i = Int32(poke);
    else if (Is_Blank(poke)) {
        n--;
        CLEAR(dat + n, MAX_TUPLE - n);
        VAL_TUPLE_LEN(value) = n;
        return;
    }
    else
        panic (Error_Invalid(poke));

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
Bounce PD_Tuple(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    if (opt_setval) {
        //
        // Returning BOUNCE_IMMEDIATE means it is up to the caller to decide if
        // they can meaningfully find a variable to store any updates to.
        //
        Poke_Tuple_Immediate(pvs->out, picker, opt_setval);
        return BOUNCE_IMMEDIATE;
    }

    Pick_Tuple(pvs->out, pvs->out, picker);
    return pvs->out;
}


//
//  MF_Tuple: C
//
void MF_Tuple(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    // "Buffer must be large enough to hold longest tuple.
    //  Longest is: (3 digits + '.') * 11 nums + 1 term => 45"
    //
    // !!! ^-- Out of date comments; TUPLE! needs review and replacement.
    //
    Byte buf[60];

    REBLEN len = VAL_TUPLE_LEN(v);
    const Byte *tp = cast(const Byte *, VAL_TUPLE(v));

    Byte *out = buf;

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

    Append_Unencoded_Len(mo->utf8flex, s_cast(buf), out - buf);
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
    Value* value = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;
    const Byte *ap;
    REBLEN len;
    REBLEN alen;
    REBINT  a;
    REBDEC  dec;

    assert(Is_Tuple(value));

    Byte *vp = VAL_TUPLE(value);
    len = VAL_TUPLE_LEN(value);

    Option(SymId) sym = Word_Id(verb);

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

        if (Is_Integer(arg)) {
            dec = -207.6382; // unused but avoid maybe uninitialized warning
            a = VAL_INT32(arg);
            ap = 0;
        }
        else if (Is_Decimal(arg) || Is_Percent(arg)) {
            dec = VAL_DECIMAL(arg);
            a = cast(REBINT, dec);
            ap = 0;
        }
        else if (Is_Tuple(arg)) {
            dec = -251.8517; // unused but avoid maybe uninitialized warning
            ap = VAL_TUPLE(arg);
            alen = VAL_TUPLE_LEN(arg);
            if (len < alen)
                len = VAL_TUPLE_LEN(value) = alen;
            a = 646699; // unused but avoid maybe uninitialized warning
        }
        else
            panic (Error_Math_Args(TYPE_TUPLE, verb));

        for (;len > 0; len--, vp++) {
            REBINT v = *vp;
            if (ap)
                a = (REBINT) *ap++;

            switch (opt Word_Id(verb)) {
            case SYM_ADD: v += a; break;

            case SYM_SUBTRACT: v -= a; break;

            case SYM_MULTIPLY:
                if (Is_Decimal(arg) || Is_Percent(arg))
                    v = cast(REBINT, v * dec);
                else
                    v *= a;
                break;

            case SYM_DIVIDE:
                if (Is_Decimal(arg) || Is_Percent(arg)) {
                    if (dec == 0.0)
                        panic (Error_Zero_Divide_Raw());

                    v = cast(REBINT, Round_Dec(v / dec, 0, 1.0));
                }
                else {
                    if (a == 0)
                        panic (Error_Zero_Divide_Raw());
                    v /= a;
                }
                break;

            case SYM_REMAINDER:
                if (a == 0)
                    panic (Error_Zero_Divide_Raw());
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
                panic (Error_Illegal_Action(TYPE_TUPLE, verb));
            }

            if (v > 255)
                v = 255;
            else if (v < 0)
                v = 0;
            *vp = cast(Byte, v);
        }
        RETURN (value);
    }

    // !!!! merge with SWITCH below !!!
    if (sym == SYM_COMPLEMENT) {
        for (; len > 0; len--, vp++)
            *vp = cast(Byte, ~*vp);
        RETURN (value);
    }
    if (sym == SYM_RANDOM) {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(VALUE));

        if (Bool_ARG(ONLY))
            panic (Error_Bad_Refines_Raw());

        if (Bool_ARG(SEED))
            panic (Error_Bad_Refines_Raw());
        for (; len > 0; len--, vp++) {
            if (*vp)
                *vp = cast(Byte, Random_Int(Bool_ARG(SECURE)) % (1 + *vp));
        }
        RETURN (value);
    }

    switch (opt sym) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(VALUE));
        Option(SymId) property = Word_Id(ARG(PROPERTY));
        assert(property != SYM_0);

        switch (opt property) {
        case SYM_LENGTH:
            return Init_Integer(OUT, MAX(len, 3));

        default:
            break;
        }

        break; }

    case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;

        UNUSED(PARAM(SERIES));

        if (Bool_ARG(PART)) {
            len = Get_Num_From_Arg(ARG(LIMIT));
            len = MIN(len, VAL_TUPLE_LEN(value));
        }
        if (len > 0) {
            REBLEN i;
            //len = MAX(len, 3);
            for (i = 0; i < len/2; i++) {
                a = vp[len - i - 1];
                vp[len - i - 1] = vp[i];
                vp[i] = a;
            }
        }
        RETURN (value); }
/*
  poke_it:
        a = Get_Num_From_Arg(arg);
        if (a <= 0 || a > len) {
            if (action == A_PICK) return nullptr;
            panic (Error_Out_Of_Range(arg));
        }
        if (action == A_PICK)
            return Init_Integer(OUT, vp[a-1]);
        // Poke:
        if (not Is_Integer(D_ARG(3)))
            panic (Error_Invalid(D_ARG(3)));
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

    panic (Error_Illegal_Action(TYPE_TUPLE, verb));
}
