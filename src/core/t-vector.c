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
//      stored in series->size for now
//      [d d d d   d d d d   0 0 0 0   t s b b]

// Encoding identifiers:
enum {
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

#define VECT_TYPE(s) ((s)->misc.size & 0xff)

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


void Set_Vector_Row(REBSER *ser, REBVAL *blk)
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
        Reset_Val_Header(val, (type >= VTSF08) ? REB_DECIMAL : REB_INTEGER);
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
    REBCNT l1 = VAL_LEN_AT(v1);
    REBCNT l2 = VAL_LEN_AT(v2);
    REBCNT len = MIN(l1, l2);
    REBCNT n;
    REBU64 i1;
    REBU64 i2;
    REBYTE *d1 = SER_DATA_RAW(VAL_SERIES(v1));
    REBYTE *d2 = SER_DATA_RAW(VAL_SERIES(v2));
    REBCNT b1 = VECT_TYPE(VAL_SERIES(v1));
    REBCNT b2 = VECT_TYPE(VAL_SERIES(v2));

    if ((b1 >= VTSF08 && b2 < VTSF08) || (b2 >= VTSF08 && b1 < VTSF08))
        fail (Error_Not_Same_Type_Raw());

    for (n = 0; n < len; n++) {
        i1 = get_vect(b1, d1, n + VAL_INDEX(v1));
        i2 = get_vect(b2, d2, n + VAL_INDEX(v2));
        if (i1 != i2) break;
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
        Reset_Val_Header(var, REB_DECIMAL);
        REBU64 u =  get_vect(bits, data, index);
        Init_Decimal_Bits(var, cast(REBYTE*, &u));
    }
    else {
        Reset_Val_Header(var, REB_INTEGER);
        VAL_INT64(var) = get_vect(bits, data, index);
    }
}


//
//  Make_Vector: C
//
// type: the datatype
// sign: signed or unsigned
// dims: number of dimensions
// bits: number of bits per unit (8, 16, 32, 64)
// size: size of array ?
//
REBSER *Make_Vector(REBINT type, REBINT sign, REBINT dims, REBINT bits, REBINT size)
{
    REBCNT len = size * dims;
    if (len > 0x7fffffff)
        fail ("vector size too big");

    REBSER *ser = Make_Series_Core(len + 1, bits/8, SERIES_FLAG_POWER_OF_2);
    CLEAR(SER_DATA_RAW(ser), (len * bits) / 8);
    SET_SERIES_LEN(ser, len);

    // Store info about the vector (could be moved to flags if necessary):
    switch (bits) {
    case  8: bits = 0; break;
    case 16: bits = 1; break;
    case 32: bits = 2; break;
    case 64: bits = 3; break;
    }
    ser->misc.size = (dims << 8) | (type << 3) | (sign << 2) | bits;

    return ser;
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
REBVAL *Make_Vector_Spec(RELVAL *bp, REBSPC *specifier, REBVAL *value)
{
    REBINT type = -1; // 0 = int,    1 = float
    REBINT sign = -1; // 0 = signed, 1 = unsigned
    REBINT dims = 1;
    REBINT bits = 32;
    REBCNT size = 1;
    REBSER *vect;
    REBVAL *iblk = 0;

    if (specifier) {
        //
        // The specifier would be needed if variables were going to be looked
        // up, but isn't required for just symbol comparisons or extracting
        // integer values.
    }

    // UNSIGNED
    if (IS_WORD(bp) && VAL_WORD_SYM(bp) == SYM_UNSIGNED) {
        sign = 1;
        bp++;
    }

    // INTEGER! or DECIMAL!
    if (IS_WORD(bp)) {
        if (SAME_SYM_NONZERO(VAL_WORD_SYM(bp), SYM_FROM_KIND(REB_INTEGER)))
            type = 0;
        else if (
            SAME_SYM_NONZERO(VAL_WORD_SYM(bp), SYM_FROM_KIND(REB_DECIMAL))
        ){
            type = 1;
            if (sign > 0) return 0;
        }
        else return 0;
        bp++;
    }

    if (type < 0) type = 0;
    if (sign < 0) sign = 0;

    // BITS
    if (IS_INTEGER(bp)) {
        bits = Int32(KNOWN(bp));
        if (
            (bits == 32 || bits == 64)
            ||
            (type == 0 && (bits == 8 || bits == 16))
        ) bp++;
        else return 0;
    } else return 0;

    // SIZE
    if (NOT_END(bp) && IS_INTEGER(bp)) {
        if (Int32(KNOWN(bp)) < 0) return 0;
        size = Int32(KNOWN(bp));
        bp++;
    }

    // Initial data:
    if (NOT_END(bp) && (IS_BLOCK(bp) || IS_BINARY(bp))) {
        REBCNT len = VAL_LEN_AT(bp);
        if (IS_BINARY(bp) && type == 1) return 0;
        if (len > size) size = len;
        iblk = KNOWN(bp);
        bp++;
    }

    Reset_Val_Header(value, REB_VECTOR);

    // Index offset:
    if (NOT_END(bp) && IS_INTEGER(bp)) {
        VAL_INDEX(value) = (Int32s(KNOWN(bp), 1) - 1);
        bp++;
    }
    else VAL_INDEX(value) = 0;

    if (NOT_END(bp)) return 0;

    vect = Make_Vector(type, sign, dims, bits, size);
    if (!vect) return 0;

    if (iblk) Set_Vector_Row(vect, iblk);

    INIT_VAL_SERIES(value, vect);
    MANAGE_SERIES(vect);

    // index set earlier

    return value;
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
        if (Make_Vector_Spec(VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg), out))
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
        Reset_Val_Header(out, REB_DECIMAL);
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
REBINT PD_Vector(REBPVS *pvs)
{
    if (pvs->opt_setval) {
        Poke_Vector_Fail_If_Read_Only(
            KNOWN(pvs->value), pvs->selector, pvs->opt_setval
        );
        return PE_OK;
    }

    Pick_Vector(pvs->store, KNOWN(pvs->value), pvs->selector);
    return PE_USE_STORE;
}


//
//  REBTYPE: C
//
REBTYPE(Vector)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBSER *ser;

    // Common operations for any series type (length, head, etc.)
    {
        REB_R r = Series_Common_Action_Maybe_Unhandled(frame_, action);
        if (r != R_UNHANDLED)
            return r;
    }

    REBSER *vect = VAL_SERIES(value);

    switch (action) {

    case SYM_PICK_P:
        Pick_Vector(D_OUT, value, arg);
        return R_OUT;

    case SYM_POKE:
        Poke_Vector_Fail_If_Read_Only(value, arg, D_ARG(3));
        Move_Value(D_OUT, D_ARG(3));
        return R_OUT;

    case SYM_LENGTH:
        //bits = 1 << (vect->size & 3);
        Init_Integer(D_OUT, SER_LEN(vect));
        return R_OUT;

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
        ser->misc.size = vect->misc.size; // attributes
        Init_Vector(value, ser);
        break; }

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
        fail (Error_Illegal_Action(VAL_TYPE(value), action));
    }

    Move_Value(D_OUT, value);
    return R_OUT;
}


//
//  Mold_Vector: C
//
void Mold_Vector(const REBVAL *value, REB_MOLD *mold, REBOOL molded)
{
    REBSER *vect = VAL_SERIES(value);
    REBYTE *data = SER_DATA_RAW(vect);
    REBCNT bits  = VECT_TYPE(vect);
//  REBCNT dims  = vect->size >> 8;
    REBCNT len;
    REBCNT n;
    REBCNT c;
    union {REBU64 i; REBDEC d;} v;
    REBYTE buf[32];
    REBYTE l;

    if (GET_MOPT(mold, MOPT_MOLD_ALL)) {
        len = VAL_LEN_HEAD(value);
        n = 0;
    } else {
        len = VAL_LEN_AT(value);
        n = VAL_INDEX(value);
    }

    if (molded) {
        enum Reb_Kind kind = (bits >= VTSF08) ? REB_DECIMAL : REB_INTEGER;
        Pre_Mold(value, mold);
        if (!GET_MOPT(mold, MOPT_MOLD_ALL))
            Append_Codepoint_Raw(mold->series, '[');
        if (bits >= VTUI08 && bits <= VTUI64)
            Append_Unencoded(mold->series, "unsigned ");
        Emit(
            mold,
            "N I I [",
            Canon(SYM_FROM_KIND(kind)),
            bit_sizes[bits & 3],
            len
        );
        if (len)
            New_Indented_Line(mold);
    }

    c = 0;
    for (; n < SER_LEN(vect); n++) {
        v.i = get_vect(bits, data, n);
        if (bits < VTSF08) {
            l = Emit_Integer(buf, v.i);
        } else {
            l = Emit_Decimal(buf, v.d, 0, '.', mold->digits);
        }
        Append_Unencoded_Len(mold->series, s_cast(buf), l);

        if ((++c > 7) && (n + 1 < SER_LEN(vect))) {
            New_Indented_Line(mold);
            c = 0;
        }
        else
            Append_Codepoint_Raw(mold->series, ' ');
    }

    if (len) {
        //
        // remove final space (overwritten with terminator)
        //
        TERM_UNI_LEN(mold->series, UNI_LEN(mold->series) - 1);
    }

    if (molded) {
        if (len) New_Indented_Line(mold);
        Append_Codepoint_Raw(mold->series, ']');
        if (!GET_MOPT(mold, MOPT_MOLD_ALL)) {
            Append_Codepoint_Raw(mold->series, ']');
        }
        else {
            Post_Mold(value, mold);
        }
    }
}
