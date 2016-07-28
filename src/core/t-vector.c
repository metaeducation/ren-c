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

#define Val_Init_Vector(v,s) \
    Val_Init_Series((v), REB_VECTOR, (s))

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
    REBYTE *data = SER_DATA_RAW(VAL_SERIES(vect));
    REBCNT type = VECT_TYPE(VAL_SERIES(vect));
    REBARR *array = NULL;
    REBCNT n;
    RELVAL *val;

    if (len <= 0)
        fail (Error_Invalid_Arg(vect));

    array = Make_Array(len);
    val = ARR_HEAD(array);
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
        fail (Error(RE_NOT_SAME_TYPE));

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
        VAL_RESET_HEADER(var, REB_DECIMAL);
        INIT_DECIMAL_BITS(var, get_vect(bits, data, index));
    }
    else {
        VAL_RESET_HEADER(var, REB_INTEGER);
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
    REBCNT len;
    REBSER *ser;

    len = size * dims;
    if (len > 0x7fffffff) return 0;
    // !!! can width help extend the len?
    ser = Make_Series(len + 1, bits/8, MKS_NONE | MKS_POWER_OF_2);
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
REBVAL *Make_Vector_Spec(RELVAL *bp, REBCTX *specifier, REBVAL *value)
{
    REBINT type = -1; // 0 = int,    1 = float
    REBINT sign = -1; // 0 = signed, 1 = unsigned
    REBINT dims = 1;
    REBINT bits = 32;
    REBCNT size = 1;
    REBSER *vect;
    REBVAL *iblk = 0;

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

    VAL_RESET_HEADER(value, REB_VECTOR);

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
        Val_Init_Vector(out, ser);
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
//  PD_Vector: C
//
REBINT PD_Vector(REBPVS *pvs)
{
    const REBVAL *setval;
    REBSER *vect;
    REBINT n;
    REBINT bits;
    REBYTE *vp;
    REBI64 i;
    REBDEC f;

    if (IS_INTEGER(pvs->selector) || IS_DECIMAL(pvs->selector))
        n = Int32(pvs->selector);
    else
        fail (Error_Bad_Path_Select(pvs));

    n += VAL_INDEX(pvs->value);
    vect = VAL_SERIES(pvs->value);
    vp = SER_DATA_RAW(vect);
    bits = VECT_TYPE(vect);

    if (!(setval = pvs->opt_setval)) {

        // Check range:
        if (n <= 0 || cast(REBCNT, n) > SER_LEN(vect)) return PE_NONE;

        // Get element value:
        if (bits < VTSF08) {
            VAL_RESET_HEADER(pvs->store, REB_INTEGER);
            VAL_INT64(pvs->store) = get_vect(bits, vp, n - 1); // 64 bits
        }
        else {
            VAL_RESET_HEADER(pvs->store, REB_DECIMAL);
            INIT_DECIMAL_BITS(pvs->store, get_vect(bits, vp, n - 1)); // 64bit
        }

        return PE_USE_STORE;
    }

    //--- Set Value...
    FAIL_IF_LOCKED_SERIES(vect);

    if (n <= 0 || cast(REBCNT, n) > SER_LEN(vect))
        fail (Error_Bad_Path_Range(pvs));

    if (IS_INTEGER(setval)) {
        i = VAL_INT64(setval);
        if (bits > VTUI64) f = cast(REBDEC, i);
        else {
            // !!! REVIEW: f was not set in this case; compiler caught the
            // unused parameter.  So fill with distinctive garbage to make it
            // easier to search for if it ever is.
            f = -646.699;
        }
    }
    else if (IS_DECIMAL(setval)) {
        f = VAL_DECIMAL(setval);
        if (bits <= VTUI64)
            i = cast(REBINT, f);
        else
            fail (Error_Bad_Path_Set(pvs));
    }
    else fail (Error_Bad_Path_Set(pvs));

    set_vect(bits, vp, n-1, i, f);

    return PE_OK;
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
        REB_R r;
        if (Series_Common_Action_Returns(&r, frame_, action))
            return r;
    }

    REBSER *vect = VAL_SERIES(value);

    switch (action) {

    case SYM_PICK:
        Pick_Path(D_OUT, value, arg, NULL);
        return R_OUT;

    case SYM_POKE:
        FAIL_IF_LOCKED_SERIES(vect);
        // Third argument to pick path is the
        Pick_Path(D_OUT, value, arg, D_ARG(3));
        *D_OUT = *D_ARG(3);
        return R_OUT;

    case SYM_LENGTH:
        //bits = 1 << (vect->size & 3);
        SET_INTEGER(D_OUT, SER_LEN(vect));
        return R_OUT;

    case SYM_COPY:
        ser = Copy_Sequence(vect);
        ser->misc.size = vect->misc.size; // attributes
        Val_Init_Vector(value, ser);
        break;

    case SYM_RANDOM:
        FAIL_IF_LOCKED_SERIES(vect);

        if (D_REF(2) || D_REF(4)) fail (Error(RE_BAD_REFINES)); // /seed /only
        Shuffle_Vector(value, D_REF(3));
        *D_OUT = *D_ARG(1);
        return R_OUT;

    default:
        fail (Error_Illegal_Action(VAL_TYPE(value), action));
    }

    *D_OUT = *value;
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
        // remove final space
        //
        SET_SERIES_LEN(mold->series, SER_LEN(mold->series) - 1);
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
