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

#include "sys-core.h"

#define Init_Vector(v,s) \
    Init_Any_Series((v), REB_VECTOR, (s))

// Encoding Format:
//      stored in series->vect_type for now
//      [d d d d   d d d d   0 0 0 0   t s b b]

enum Reb_Vect_Type {
    VTSI08 = 0,
    VTSI16,
    VTSI32,
    VTSI64,

    VTUI08,
    VTUI16,
    VTUI32,
    VTUI64,

    VTSF08,     // not used
    VTSF16,     // not used
    VTSF32,
    VTSF64
};

#define VECT_TYPE(s) \
    MISC(s).vect_info.type

static REBCNT bit_sizes[4] = {8, 16, 32, 64};

REBU64 f_to_u64(float n) {
    union {
        REBU64 u;
        REBDEC d;
    } t;
    t.d = n;
    return t.u;
}


REBU64 get_vect(REBCNT bits, REBYTE *data, REBCNT n)
{
    switch (bits) {
    case VTSI08:
        return (REBI64) ((i8*)data)[n];

    case VTSI16:
        return (REBI64) ((i16*)data)[n];

    case VTSI32:
        return (REBI64) ((i32*)data)[n];

    case VTSI64:
        return (REBI64) ((i64*)data)[n];

    case VTUI08:
        return (REBU64) ((u8*)data)[n];

    case VTUI16:
        return (REBU64) ((u16*)data)[n];

    case VTUI32:
        return (REBU64) ((u32*)data)[n];

    case VTUI64:
        return (REBU64) ((i64*)data)[n];

    case VTSF08:
    case VTSF16:
    case VTSF32:
        return f_to_u64(((float*)data)[n]);

    case VTSF64:
        return ((REBU64*)data)[n];
    }

    return 0;
}

void set_vect(REBCNT bits, REBYTE *data, REBCNT n, REBI64 i, REBDEC f) {
    switch (bits) {

    case VTSI08:
        ((i8*)data)[n] = (i8)i;
        break;

    case VTSI16:
        ((i16*)data)[n] = (i16)i;
        break;

    case VTSI32:
        ((i32*)data)[n] = (i32)i;
        break;

    case VTSI64:
        ((i64*)data)[n] = (i64)i;
        break;

    case VTUI08:
        ((u8*)data)[n] = (u8)i;
        break;

    case VTUI16:
        ((u16*)data)[n] = (u16)i;
        break;

    case VTUI32:
        ((u32*)data)[n] = (u32)i;
        break;

    case VTUI64:
        ((i64*)data)[n] = (u64)i;
        break;

    case VTSF08:
    case VTSF16:
    case VTSF32:
        ((float*)data)[n] = (float)f;
        break;

    case VTSF64:
        ((double*)data)[n] = f;
        break;
    }
}


void Set_Vector_Row(REBSER *ser, const REBVAL *blk)
{
    REBCNT idx = VAL_INDEX(blk);
    REBCNT len = VAL_LEN_AT(blk);
    RELVAL *val;
    REBCNT n = 0;
    REBCNT bits = VECT_TYPE(ser);
    REBI64 i = 0;
    REBDEC f = 0;

    if (IS_BLOCK(blk)) {
        val = VAL_ARRAY_AT(blk);

        for (; NOT_END(val); val++) {
            if (IS_INTEGER(val)) {
                i = VAL_INT64(val);
                if (bits > VTUI64) f = (REBDEC)(i);
            }
            else if (IS_DECIMAL(val)) {
                f = VAL_DECIMAL(val);
                if (bits <= VTUI64) i = (REBINT)(f);
            }
            else fail (Error_Invalid_Arg_Core(val, VAL_SPECIFIER(blk)));
            //if (n >= ser->tail) Expand_Vector(ser);
            set_vect(bits, SER_DATA_RAW(ser), n++, i, f);
        }
    }
    else {
        REBYTE *data = VAL_BIN_AT(blk);
        for (; len > 0; len--, idx++) {
            set_vect(
                bits, SER_DATA_RAW(ser), n++, cast(REBI64, data[idx]), f
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
        fail (vect);

    REBARR *array = Make_Array(len);

    REBYTE *data = SER_DATA_RAW(VAL_SERIES(vect));
    REBCNT type = VECT_TYPE(VAL_SERIES(vect));

    RELVAL *val = ARR_HEAD(array);
    REBCNT n;
    for (n = VAL_INDEX(vect); n < VAL_LEN_HEAD(vect); n++, val++) {
        VAL_RESET_HEADER(val, (type >= VTSF08) ? REB_DECIMAL : REB_INTEGER);
        VAL_INT64(val) = get_vect(type, data, n); // can be int or decimal
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
    REBCNT b1 = VECT_TYPE(VAL_SERIES(v1));
    REBCNT b2 = VECT_TYPE(VAL_SERIES(v2));

    if ((b1 >= VTSF08 && b2 < VTSF08) || (b2 >= VTSF08 && b1 < VTSF08))
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
        i1 = get_vect(b1, d1, n + VAL_INDEX(v1));
        i2 = get_vect(b2, d2, n + VAL_INDEX(v2));
        if (i1 != i2)
            break;
    }

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
    REBCNT n;
    REBCNT k;
    REBU64 swap;
    REBYTE *data = SER_DATA_RAW(VAL_SERIES(vect));
    REBCNT type = VECT_TYPE(VAL_SERIES(vect));
    REBCNT idx = VAL_INDEX(vect);

    // We can do it as INTS, because we just deal with the bits:
    if (type == VTSF32) type = VTUI32;
    else if (type == VTSF64) type = VTUI64;

    for (n = VAL_LEN_AT(vect); n > 1;) {
        k = idx + (REBCNT)Random_Int(secure) % n;
        n--;
        swap = get_vect(type, data, k);
        set_vect(type, data, k, get_vect(type, data, n + idx), 0);
        set_vect(type, data, n + idx, swap, 0);
    }
}


//
//  Set_Vector_Value: C
//
void Set_Vector_Value(REBVAL *var, REBSER *series, REBCNT index)
{
    REBYTE *data = SER_DATA_RAW(series);
    REBCNT bits = VECT_TYPE(series);

    if (bits >= VTSF08) {
        VAL_RESET_HEADER(var, REB_DECIMAL);
        REBU64 u =  get_vect(bits, data, index);
        Init_Decimal_Bits(var, cast(REBYTE*, &u));
    }
    else {
        VAL_RESET_HEADER(var, REB_INTEGER);
        VAL_INT64(var) = get_vect(bits, data, index);
    }
}


//
//  Make_Vector: C
//
static REBSER *Make_Vector(
    REBINT type, // actually a Reb_Vect_Type
    REBINT sign, // signed or unsigned
    REBINT dims, // number of dimensions
    REBINT bits, // number of bits per unit (8, 16, 32, 64)
    REBINT size // !!! "size of array ?"
){
    REBCNT len = size * dims;
    if (len > 0x7fffffff)
        fail ("vector size too big");

    REBSER *s = Make_Series_Core(len + 1, bits / 8, SERIES_FLAG_POWER_OF_2);
    CLEAR(SER_DATA_RAW(s), (len * bits) / 8);
    SET_SERIES_LEN(s, len);

    MISC(s).vect_info.type = type;

    switch (bits) {
    case 8:
        MISC(s).vect_info.bits = 0;
        break;

    case 16:
        MISC(s).vect_info.bits = 1;
        break;

    case 32:
        MISC(s).vect_info.bits = 2;
        break;

    case 64:
        MISC(s).vect_info.bits = 3;
        break;
    }

    MISC(s).vect_info.sign = sign;
    MISC(s).vect_info.dims = dims;

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
    REBINT type = -1; // 0 = int,    1 = float
    REBINT sign = -1; // 0 = signed, 1 = unsigned
    REBINT dims = 1;
    REBINT bits = 32;
    REBCNT size = 1;

    const RELVAL *item = head;

    if (specifier) {
        //
        // The specifier would be needed if variables were going to be looked
        // up, but isn't required for just symbol comparisons or extracting
        // integer values.
    }

    // UNSIGNED
    if (IS_WORD(item) && VAL_WORD_SYM(item) == SYM_UNSIGNED) {
        sign = 1;
        ++item;
    }

    // INTEGER! or DECIMAL!
    if (IS_WORD(item)) {
        if (SAME_SYM_NONZERO(VAL_WORD_SYM(item), SYM_FROM_KIND(REB_INTEGER)))
            type = 0;
        else if (
            SAME_SYM_NONZERO(VAL_WORD_SYM(item), SYM_FROM_KIND(REB_DECIMAL))
        ){
            type = 1;
            if (sign > 0)
                return FALSE;
        }
        else
            return FALSE;
        ++item;
    }

    if (type < 0)
        type = 0;
    if (sign < 0)
        sign = 0;

    // BITS
    if (IS_INTEGER(item)) {
        bits = Int32(item);
        if (
            (bits == 32 || bits == 64)
            || (type == 0 && (bits == 8 || bits == 16))
        ){
            ++item;
        }
        else
            return FALSE;
    }
    else
        return FALSE;

    // SIZE
    if (NOT_END(item) && IS_INTEGER(item)) {
        if (Int32(item) < 0)
            return FALSE;
        size = Int32(item);
        ++item;
    }

    // Initial data:

    const REBVAL *iblk;
    if (NOT_END(item) && (IS_BLOCK(item) || IS_BINARY(item))) {
        REBCNT len = VAL_LEN_AT(item);
        if (IS_BINARY(item) && type == 1)
            return FALSE;
        if (len > size)
            size = len;
        iblk = const_KNOWN(item);
        ++item;
    }
    else
        iblk = NULL;

    // Index offset:
    REBCNT index;
    if (NOT_END(item) && IS_INTEGER(item)) {
        index = (Int32s(item, 1) - 1);
        ++item;
    }
    else
        index = 0;

    if (NOT_END(item))
        return FALSE;

    REBSER *vect = Make_Vector(type, sign, dims, bits, size);
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
        if (size < 0) goto bad_make;
        REBSER *ser = Make_Vector(0, 0, 1, 32, size);
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
        fail (picker);

    n += VAL_INDEX(value);

    if (n <= 0 || cast(REBCNT, n) > SER_LEN(vect)) {
        Init_Void(out); // out of range of vector data
        return;
    }

    REBYTE *vp = SER_DATA_RAW(vect);
    REBINT bits = VECT_TYPE(vect);

    if (bits < VTSF08)
        Init_Integer(out, get_vect(bits, vp, n - 1)); // 64-bit
    else {
        VAL_RESET_HEADER(out, REB_DECIMAL);
        REBI64 i = get_vect(bits, vp, n - 1);
        Init_Decimal_Bits(out, cast(REBYTE*, &i));
    }
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
        fail (picker);

    n += VAL_INDEX(value);

    if (n <= 0 || cast(REBCNT, n) > SER_LEN(vect))
        fail (Error_Out_Of_Range(picker));

    REBYTE *vp = SER_DATA_RAW(vect);
    REBINT bits = VECT_TYPE(vect);

    REBI64 i;
    REBDEC f;
    if (IS_INTEGER(poke)) {
        i = VAL_INT64(poke);
        if (bits > VTUI64)
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
        if (bits <= VTUI64)
            i = cast(REBINT, f);
        else
            i = 0xDECAFBAD; // not used, but avoid maybe uninitalized warning
    }
    else
        fail (poke);

    set_vect(bits, vp, n - 1, i, f);
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
        REB_R r = Series_Common_Action_Maybe_Unhandled(frame_, action);
        if (r != R_UNHANDLED)
            return r;
    }

    REBSER *vect = VAL_SERIES(value);

    switch (action) {

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

    fail (Error_Illegal_Action(VAL_TYPE(value), action));

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
    REBCNT bits = VECT_TYPE(vect);

    REBCNT len;
    REBCNT n;
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
        len = VAL_LEN_HEAD(v);
        n = 0;
    } else {
        len = VAL_LEN_AT(v);
        n = VAL_INDEX(v);
    }

    if (NOT(form)) {
        enum Reb_Kind kind = (bits >= VTSF08) ? REB_DECIMAL : REB_INTEGER;
        Pre_Mold(mo, v);
        if (NOT_MOLD_FLAG(mo, MOLD_FLAG_ALL))
            Append_Utf8_Codepoint(mo->series, '[');
        if (bits >= VTUI08 && bits <= VTUI64)
            Append_Unencoded(mo->series, "unsigned ");
        Emit(
            mo,
            "N I I [",
            Canon(SYM_FROM_KIND(kind)),
            bit_sizes[bits & 3],
            len
        );
        if (len)
            New_Indented_Line(mo);
    }

    REBCNT c = 0;
    for (; n < SER_LEN(vect); n++) {
        union {REBU64 i; REBDEC d;} u;

        u.i = get_vect(bits, data, n);

        REBYTE buf[32];
        REBYTE l;
        if (bits < VTSF08) {
            l = Emit_Integer(buf, u.i);
        } else {
            l = Emit_Decimal(buf, u.d, 0, '.', mo->digits);
        }
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

    if (NOT(form)) {
        if (len)
            New_Indented_Line(mo);

        Append_Utf8_Codepoint(mo->series, ']');

        if (NOT_MOLD_FLAG(mo, MOLD_FLAG_ALL))
            Append_Utf8_Codepoint(mo->series, ']');
        else
            Post_Mold(mo, v);
    }
}
