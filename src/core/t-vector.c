//
//  File: %t-vector.c
//  Summary: "vector datatype"
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
// !!! The VECTOR! datatype was a largely unused/untested feature of R3-Alpha,
// the goal of which was to store and process raw packed integers/floats, in
// a more convenient way than using a BINARY!.  User attempts to extend this
// to multi-dimensional matrix also happened after the R3-Alpha release.
//
// Keeping the code in this form around is of questionable value in Ren-C,
// but it has been kept alive mostly for purposes of testing FFI callbacks
// (e.g. qsort()) by giving Rebol a very limited ability to work with packed
// C-style memory blocks.
//
// Ultimately it is kept as a bookmark for what a user-defined type in an
// extension might have to deal with to bridge Rebol userspace to vector data.
//

#include "sys-core.h"

#define Init_Vector(v,s) \
    Init_Any_Series((v), REB_VECTOR, (s))


// !!! Routines in the vector code seem to want to make it easy to exchange
// blobs of data without knowing what's in them.  This has led to what is
// likely undefined behavior, casting REBDEC to REBU64 etc.  It all needs
// a lot of review if this code is ever going to be used for anything real.
//
REBU64 f_to_u64(float n) {
    union {
        REBU64 u;
        REBDEC d;
    } t;
    t.d = n;
    return t.u;
}


// !!! This routine appears to get whatever the data element type is of the
// vector back as an unsigned 64 bit quantity...even if it's floating point.
//
REBU64 get_vect(
    REBOOL non_integer, REBOOL sign, REBCNT bits,
    REBYTE *data, REBCNT n
){
    if (non_integer) {
        assert(sign);

        switch (bits) {
        case 32:
            return f_to_u64(((float*)data)[n]);

        case 64:
            return cast(uint64_t*, data)[n];
        }
    }
    else {
        if (sign) {
            switch (bits) {
            case 8:
                return cast(int64_t, cast(int8_t*, data)[n]);

            case 16:
                return cast(int64_t, cast(int16_t*, data)[n]);

            case 32:
                return cast(int64_t, cast(int32_t*, data)[n]);

            case 64:
                return cast(int64_t, cast(int64_t*, data)[n]);
            }
        }
        else {
            switch (bits) {
            case 8:
                return cast(uint64_t, cast(uint8_t*, data)[n]);

            case 16:
                return cast(uint64_t, cast(uint16_t*, data)[n]);

            case 32:
                return cast(uint64_t, cast(uint32_t*, data)[n]);

            case 64:
                return cast(uint64_t, cast(int64_t*, data)[n]); // !!! signed?
            }
        }
    }

    panic ("Unsupported vector element sign/type/size combination");
}

void set_vect(
    REBOOL non_integer, REBOOL sign, REBCNT bits,
    REBYTE *data, REBCNT n, REBI64 i, REBDEC f
){
    if (non_integer) {
        assert(sign);

        switch (bits) {
        case 32:
            ((float*)data)[n] = (float)f;
            return;

        case 64:
            ((double*)data)[n] = f;
            return;
        }
    }
    else {
        if (sign) {
            switch (bits) {
            case 8:
                cast(int8_t*, data)[n] = cast(int8_t, i);
                return;

            case 16:
                cast(int16_t*, data)[n] = cast(int16_t, i);
                return;

            case 32:
                cast(int32_t*, data)[n] = cast(int32_t, i);
                return;

            case 64:
                cast(int64_t*, data)[n] = cast(int64_t, i);
                return;
            }
        }
        else {
            switch (bits) {
            case 8:
                cast(uint8_t*, data)[n] = cast(uint8_t, i);
                return;

            case 16:
                cast(uint16_t*, data)[n] = cast(uint16_t, i);
                return;

            case 32:
                cast(uint32_t*, data)[n] = cast(uint32_t, i);
                return;

            case 64:
                cast(int64_t*, data)[n] = cast(uint64_t, i); // !!! signed?
                return;
            }
        }
    }

    panic ("Unsupported vector element sign/type/size combination");
}


void Set_Vector_Row(REBSER *ser, const REBVAL *blk)
{
    REBCNT idx = VAL_INDEX(blk);
    REBCNT len = VAL_LEN_AT(blk);
    RELVAL *val;
    REBCNT n = 0;
    REBI64 i = 0;
    REBDEC f = 0;

    REBOOL non_integer = (MISC(ser).vect_info.non_integer == 1);
    REBOOL sign = (MISC(ser).vect_info.sign == 1);
    REBCNT bits = MISC(ser).vect_info.bits;

    if (IS_BLOCK(blk)) {
        val = VAL_ARRAY_AT(blk);

        for (; NOT_END(val); val++) {
            if (IS_INTEGER(val)) {
                i = VAL_INT64(val);
                if (non_integer)
                    f = (REBDEC)(i);
            }
            else if (IS_DECIMAL(val)) {
                f = VAL_DECIMAL(val);
                if (not non_integer)
                    i = (REBINT)(f);
            }
            else
                fail (Error_Invalid_Core(val, VAL_SPECIFIER(blk)));

            //if (n >= ser->tail) Expand_Vector(ser);

            set_vect(non_integer, sign, bits, SER_DATA_RAW(ser), n++, i, f);
        }
    }
    else {
        REBYTE *data = VAL_BIN_AT(blk);
        for (; len > 0; len--, idx++) {
            set_vect(
                non_integer, sign, bits,
                SER_DATA_RAW(ser), n++, cast(REBI64, data[idx]), f
            );
        }
    }
}


//
//  Vector_To_Array: C
//
// Convert a vector to a block.
//
REBARR *Vector_To_Array(const REBVAL *vect)
{
    REBCNT len = VAL_LEN_AT(vect);
    if (len <= 0)
        fail (Error_Invalid(vect));

    REBARR *array = Make_Array(len);

    REBSER *ser = VAL_SERIES(vect);

    REBYTE *data = SER_DATA_RAW(ser);

    REBOOL non_integer = (MISC(ser).vect_info.non_integer == 1);
    REBOOL sign = (MISC(ser).vect_info.sign == 1);
    REBCNT bits = MISC(ser).vect_info.bits;

    RELVAL *val = ARR_HEAD(array);
    REBCNT n;
    for (n = VAL_INDEX(vect); n < VAL_LEN_HEAD(vect); n++, val++) {
        if (non_integer) {
            REBU64 u = get_vect(non_integer, sign, bits, data, n);
            Init_Decimal_Bits(val, cast(REBYTE*, &u));
        }
        else
            Init_Integer(val, get_vect(non_integer, sign, bits, data, n));
    }

    TERM_ARRAY_LEN(array, len);
    assert(IS_END(val));

    return array;
}


//
//  Compare_Vector: C
//
REBINT Compare_Vector(const RELVAL *v1, const RELVAL *v2)
{
    REBSER *ser1 = VAL_SERIES(v1);
    REBOOL non_integer1 = (MISC(ser1).vect_info.non_integer == 1);
    REBOOL sign1 = (MISC(ser1).vect_info.sign == 1);
    REBCNT bits1 = MISC(ser1).vect_info.bits;

    REBSER *ser2 = VAL_SERIES(v2);
    REBOOL non_integer2 = (MISC(ser2).vect_info.non_integer == 1);
    REBOOL sign2 = (MISC(ser2).vect_info.sign == 1);
    REBCNT bits2 = MISC(ser2).vect_info.bits;

    if (non_integer1 != non_integer2)
        fail (Error_Not_Same_Type_Raw());

    REBCNT l1 = VAL_LEN_AT(v1);
    REBCNT l2 = VAL_LEN_AT(v2);
    REBCNT len = MIN(l1, l2);

    REBYTE *d1 = SER_DATA_RAW(VAL_SERIES(v1));
    REBYTE *d2 = SER_DATA_RAW(VAL_SERIES(v2));

    REBU64 i1 = 0; // avoid uninitialized warning
    REBU64 i2 = 0; // ...
    REBCNT n;
    for (n = 0; n < len; n++) {
        i1 = get_vect(non_integer1, sign1, bits1, d1, n + VAL_INDEX(v1));
        i2 = get_vect(non_integer2, sign2, bits2, d2, n + VAL_INDEX(v2));
        if (i1 != i2)
            break;
    }

    // !!! This is comparing unsigned integer representations of signed or
    // possibly floating point quantities.  While that may give a *consistent*
    // ordering for sorting, it's not particularly *meaningful*.
    //
    if (n != len) {
        if (i1 > i2) return 1;
        return -1;
    }

    return l1 - l2;
}


//
//  Shuffle_Vector: C
//
void Shuffle_Vector(REBVAL *vect, REBOOL secure)
{
    REBSER *ser = VAL_SERIES(vect);

    REBYTE *data = SER_DATA_RAW(ser);
    REBCNT idx = VAL_INDEX(vect);

    // We can do it as INTS, because we just deal with the bits:

    const REBOOL non_integer = FALSE;
    REBOOL sign = (MISC(ser).vect_info.sign == 1);
    REBCNT bits = MISC(ser).vect_info.bits;

    REBCNT n;
    for (n = VAL_LEN_AT(vect); n > 1;) {
        REBCNT k = idx + (REBCNT)Random_Int(secure) % n;
        n--;
        REBU64 swap = get_vect(non_integer, sign, bits, data, k);
        set_vect(
            non_integer, sign, bits,
            data, k, get_vect(non_integer, sign, bits, data, n + idx), 0
        );
        set_vect(
            non_integer, sign, bits,
            data, n + idx, swap, 0
        );
    }
}


//
//  Set_Vector_Value: C
//
void Set_Vector_Value(REBVAL *var, REBSER *series, REBCNT index)
{
    REBYTE *data = SER_DATA_RAW(series);

    REBOOL non_integer = (MISC(series).vect_info.non_integer == 1);
    REBOOL sign = (MISC(series).vect_info.sign == 1);
    REBCNT bits = MISC(series).vect_info.bits;

    if (non_integer) {
        REBU64 u = get_vect(non_integer, sign, bits, data, index);
        Init_Decimal_Bits(var, cast(REBYTE*, &u));
    }
    else
        Init_Integer(var, get_vect(non_integer, sign, bits, data, index));
}


//
//  Make_Vector: C
//
static REBSER *Make_Vector(
    REBOOL non_integer, // if true, it's a float/decimal, not integral
    REBOOL sign, // signed or unsigned
    REBINT dims, // number of dimensions
    REBCNT bits, // number of bits per unit (8, 16, 32, 64)
    REBINT len
){
    assert(dims == 1);
    UNUSED(dims);

    if (len > 0x7fffffff)
        fail ("vector size too big");

    REBSER *s = Make_Series_Core(len + 1, bits / 8, SERIES_FLAG_POWER_OF_2);
    CLEAR(SER_DATA_RAW(s), (len * bits) / 8);
    SET_SERIES_LEN(s, len);

    MISC(s).vect_info.non_integer = non_integer ? 1 : 0;
    MISC(s).vect_info.bits = bits;
    MISC(s).vect_info.sign = sign ? 1 : 0;

    return s;
}


//
//  Make_Vector_Spec: C
//
// Make a vector from a block spec.
//
//    make vector! [integer! 32 100]
//    make vector! [decimal! 64 100]
//    make vector! [unsigned integer! 32]
//    Fields:
//         signed:     signed, unsigned
//           datatypes:  integer, decimal
//           dimensions: 1 - N
//           bitsize:    1, 8, 16, 32, 64
//           size:       integer units
//           init:        block of values
//
REBOOL Make_Vector_Spec(REBVAL *out, const RELVAL head[], REBSPC *specifier)
{
    const RELVAL *item = head;

    if (specifier) {
        //
        // The specifier would be needed if variables were going to be looked
        // up, but isn't required for just symbol comparisons or extracting
        // integer values.
    }

    REBOOL sign;
    if (IS_WORD(item) && VAL_WORD_SYM(item) == SYM_UNSIGNED) {
        sign = FALSE;
        ++item;
    }
    else
        sign = TRUE; // default to signed, not unsigned

    REBOOL non_integer;
    if (IS_WORD(item)) {
        if (SAME_SYM_NONZERO(VAL_WORD_SYM(item), SYM_FROM_KIND(REB_INTEGER)))
            non_integer = FALSE;
        else if (
            SAME_SYM_NONZERO(VAL_WORD_SYM(item), SYM_FROM_KIND(REB_DECIMAL))
        ){
            non_integer = TRUE;
            if (not sign)
                return FALSE; // C doesn't have unsigned floating points
        }
        else
            return FALSE;
        ++item;
    }
    else
        non_integer = FALSE; // default to integer, not floating point

    REBCNT bits;
    if (not IS_INTEGER(item))
        return FALSE; // bit size required, no defaulting

    bits = Int32(item);
    ++item;

    if (non_integer && (bits == 8 || bits == 16))
        return FALSE; // C doesn't have 8 or 16 bit floating points

    if (not (bits == 8 or bits == 16 or bits == 32 or bits == 64))
        return FALSE;

    REBCNT size;
    if (NOT_END(item) && IS_INTEGER(item)) {
        if (Int32(item) < 0)
            return FALSE;
        size = Int32(item);
        ++item;
    }
    else
        size = 1; // !!! default size to 1 (?)

    // Initial data:

    const REBVAL *iblk;
    if (NOT_END(item) and (IS_BLOCK(item) or IS_BINARY(item))) {
        REBCNT len = VAL_LEN_AT(item);
        if (IS_BINARY(item) and not non_integer)
            return FALSE;
        if (len > size)
            size = len;
        iblk = const_KNOWN(item);
        ++item;
    }
    else
        iblk = NULL;

    REBCNT index;
    if (NOT_END(item) && IS_INTEGER(item)) {
        index = (Int32s(item, 1) - 1);
        ++item;
    }
    else
        index = 0; // default index offset inside returned REBVAL to 0

    if (NOT_END(item))
        return FALSE;

    // !!! Dims appears to be part of unfinished work on multidimensional
    // vectors, which along with the rest of this should be storing in a
    // OBJECT!-like structure for a user-defined type, vs being bit-packed.
    //
    REBINT dims = 1;

    REBSER *vect = Make_Vector(non_integer, sign, dims, bits, size);
    if (vect == NULL)
        return FALSE;

    if (iblk != NULL)
        Set_Vector_Row(vect, iblk);

    Init_Any_Series_At(out, REB_VECTOR, vect, index);
    return TRUE;
}


//
//  MAKE_Vector: C
//
void MAKE_Vector(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    // CASE: make vector! 100
    if (IS_INTEGER(arg) || IS_DECIMAL(arg)) {
        REBINT size = Int32s(arg, 0);
        if (size < 0)
            goto bad_make;

        const REBOOL non_integer = FALSE;
        const REBOOL sign = TRUE;
        const REBINT dims = 1;
        REBSER *ser = Make_Vector(non_integer, sign, dims, 32, size);
        Init_Vector(out, ser);
        return;
    }

    TO_Vector(out, kind, arg); // may fail()
    return;

bad_make:
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Vector: C
//
void TO_Vector(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (IS_BLOCK(arg)) {
        if (Make_Vector_Spec(out, VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg)))
            return;
    }
    fail (Error_Bad_Make(kind, arg));
}


//
//  CT_Vector: C
//
REBINT CT_Vector(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBINT n = Compare_Vector(a, b);  // needs to be expanded for equality
    if (mode >= 0) {
        return n == 0;
    }
    if (mode == -1) return n >= 0;
    return n > 0;
}


//
//  Pick_Vector: C
//
void Pick_Vector(REBVAL *out, const REBVAL *value, const REBVAL *picker) {
    REBSER *vect = VAL_SERIES(value);

    REBINT n;
    if (IS_INTEGER(picker) || IS_DECIMAL(picker))
        n = Int32(picker);
    else
        fail (Error_Invalid(picker));

    n += VAL_INDEX(value);

    if (n <= 0 || cast(REBCNT, n) > SER_LEN(vect)) {
        Init_Void(out); // out of range of vector data
        return;
    }

    REBYTE *vp = SER_DATA_RAW(vect);

    REBOOL non_integer = (MISC(vect).vect_info.non_integer == 1);
    REBOOL sign = (MISC(vect).vect_info.sign == 1);
    REBCNT bits = MISC(vect).vect_info.bits;

    if (non_integer) {
        RESET_VAL_HEADER(out, REB_DECIMAL);
        REBI64 i = get_vect(non_integer, sign, bits, vp, n - 1);
        Init_Decimal_Bits(out, cast(REBYTE*, &i));
    }
    else
        Init_Integer(out, get_vect(non_integer, sign, bits, vp, n - 1));
}


//
//  Poke_Vector_Fail_If_Read_Only: C
//
void Poke_Vector_Fail_If_Read_Only(
    REBVAL *value,
    const REBVAL *picker,
    const REBVAL *poke
) {
    REBSER *vect = VAL_SERIES(value);
    FAIL_IF_READ_ONLY_SERIES(vect);

    REBINT n;
    if (IS_INTEGER(picker) || IS_DECIMAL(picker))
        n = Int32(picker);
    else
        fail (Error_Invalid(picker));

    n += VAL_INDEX(value);

    if (n <= 0 || cast(REBCNT, n) > SER_LEN(vect))
        fail (Error_Out_Of_Range(picker));

    REBYTE *vp = SER_DATA_RAW(vect);

    REBOOL non_integer = (MISC(vect).vect_info.non_integer == 1);
    REBOOL sign = (MISC(vect).vect_info.sign == 1);
    REBCNT bits = MISC(vect).vect_info.bits;

    REBI64 i;
    REBDEC f;
    if (IS_INTEGER(poke)) {
        i = VAL_INT64(poke);
        if (non_integer)
            f = cast(REBDEC, i);
        else {
            // !!! REVIEW: f was not set in this case; compiler caught the
            // unused parameter.  So fill with distinctive garbage to make it
            // easier to search for if it ever is.
            f = -646.699;
        }
    }
    else if (IS_DECIMAL(poke)) {
        f = VAL_DECIMAL(poke);
        if (non_integer)
            i = 0xDECAFBAD; // not used, but avoid maybe uninitalized warning
        else
            i = cast(REBINT, f);
    }
    else
        fail (Error_Invalid(poke));

    set_vect(non_integer, sign, bits, vp, n - 1, i, f);
}


//
//  PD_Vector: C
//
// Path dispatch acts like PICK for GET-PATH! and POKE for SET-PATH!
//
REB_R PD_Vector(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    if (opt_setval != NULL) {
        Poke_Vector_Fail_If_Read_Only(pvs->out, picker, opt_setval);
        return R_INVISIBLE;
    }

    Pick_Vector(pvs->out, pvs->out, picker);
    return R_OUT;
}


//
//  REBTYPE: C
//
REBTYPE(Vector)
{
    REBVAL *value = D_ARG(1);
    REBSER *ser;

    // Common operations for any series type (length, head, etc.)
    {
        REB_R r = Series_Common_Action_Maybe_Unhandled(frame_, verb);
        if (r != R_UNHANDLED)
            return r;
    }

    REBSER *vect = VAL_SERIES(value);

    switch (verb) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            //bits = 1 << (vect->size & 3);
            Init_Integer(D_OUT, SER_LEN(vect));
            return R_OUT;

        default:
            break;
        }

        break; }

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }

        ser = Copy_Sequence(vect);
        MISC(ser).vect_info = MISC(vect).vect_info; // attributes
        Init_Vector(value, ser);
        goto return_vector; }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;
        UNUSED(PAR(value));

        FAIL_IF_READ_ONLY_SERIES(vect);

        if (REF(seed) || REF(only))
            fail (Error_Bad_Refines_Raw());

        Shuffle_Vector(value, REF(secure));
        Move_Value(D_OUT, D_ARG(1));
        return R_OUT; }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), verb));

return_vector:
    Move_Value(D_OUT, value);
    return R_OUT;
}


//
//  MF_Vector: C
//
void MF_Vector(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    REBSER *vect = VAL_SERIES(v);
    REBYTE *data = SER_DATA_RAW(vect);

    REBCNT len;
    REBCNT n;
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
        len = VAL_LEN_HEAD(v);
        n = 0;
    } else {
        len = VAL_LEN_AT(v);
        n = VAL_INDEX(v);
    }

    REBOOL non_integer = (MISC(vect).vect_info.non_integer == 1);
    REBOOL sign = (MISC(vect).vect_info.sign == 1);
    REBCNT bits = MISC(vect).vect_info.bits;

    if (not form) {
        enum Reb_Kind kind = non_integer ? REB_DECIMAL : REB_INTEGER;
        Pre_Mold(mo, v);
        if (NOT_MOLD_FLAG(mo, MOLD_FLAG_ALL))
            Append_Utf8_Codepoint(mo->series, '[');
        if (not sign)
            Append_Unencoded(mo->series, "unsigned ");
        Emit(
            mo,
            "N I I [",
            Canon(SYM_FROM_KIND(kind)),
            bits,
            len
        );
        if (len)
            New_Indented_Line(mo);
    }

    REBCNT c = 0;
    for (; n < SER_LEN(vect); n++) {
        union {REBU64 i; REBDEC d;} u;

        u.i = get_vect(non_integer, sign, bits, data, n);

        REBYTE buf[32];
        REBYTE l;
        if (non_integer)
            l = Emit_Decimal(buf, u.d, 0, '.', mo->digits);
        else
            l = Emit_Integer(buf, u.i);
        Append_Unencoded_Len(mo->series, s_cast(buf), l);

        if ((++c > 7) && (n + 1 < SER_LEN(vect))) {
            New_Indented_Line(mo);
            c = 0;
        }
        else
            Append_Utf8_Codepoint(mo->series, ' ');
    }

    if (len) {
        //
        // remove final space (overwritten with terminator)
        //
        TERM_UNI_LEN(mo->series, UNI_LEN(mo->series) - 1);
    }

    if (not form) {
        if (len)
            New_Indented_Line(mo);

        Append_Utf8_Codepoint(mo->series, ']');

        if (NOT_MOLD_FLAG(mo, MOLD_FLAG_ALL))
            Append_Utf8_Codepoint(mo->series, ']');
        else
            Post_Mold(mo, v);
    }
}
