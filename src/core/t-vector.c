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


//
//  Get_Vector_At: C
//
void Get_Vector_At(Cell* out, REBSER *vec, REBLEN n)
{
    REBYTE *data = SER_DATA_RAW(vec);

    bool non_integer = (MISC(vec).vect_info.non_integer == 1);
    bool sign = (MISC(vec).vect_info.sign == 1);
    REBLEN bits = MISC(vec).vect_info.bits;

    if (non_integer) {
        assert(sign);

        switch (bits) {
          case 32:
            Init_Decimal(out, cast(float*, data)[n]);
            return;

          case 64:
            Init_Decimal(out, cast(double*, data)[n]);
            return;
        }
    }
    else {
        if (sign) {
            switch (bits) {
              case 8:
                Init_Integer(out, cast(int8_t*, data)[n]);
                return;

              case 16:
                Init_Integer(out, cast(int16_t*, data)[n]);
                return;

              case 32:
                Init_Integer(out, cast(int32_t*, data)[n]);
                return;

              case 64:
                Init_Integer(out, cast(int64_t*, data)[n]);
                return;
            }
        }
        else {
            switch (bits) {
              case 8:
                Init_Integer(out, cast(uint8_t*, data)[n]);
                return;

              case 16:
                Init_Integer(out, cast(uint16_t*, data)[n]);
                return;

              case 32:
                Init_Integer(out, cast(uint32_t*, data)[n]);
                return;

              case 64: {
                int64_t i = cast(int64_t*, data)[n];
                if (i < 0)
                    fail ("64-bit integer out of range for INTEGER!");
                Init_Integer(out, i);
                return; }
            }
        }
    }

    panic ("Unsupported vector element sign/type/size combination");
}


static void Set_Vector_At_Core(
    REBSER *vec,
    REBLEN n,
    const Cell* v,
    REBSPC *specifier
){
    REBYTE *data = SER_DATA_RAW(vec);

    bool non_integer = (MISC(vec).vect_info.non_integer == 1);
    bool sign = (MISC(vec).vect_info.sign == 1);
    REBLEN bits = MISC(vec).vect_info.bits;

    if (non_integer) {
        assert(sign);
        double d;
        if (IS_INTEGER(v))
            d = cast(double, VAL_INT64(v));
        else if (IS_DECIMAL(v))
            d = VAL_DECIMAL(v);
        else
            fail (Error_Invalid_Core(v, specifier));

        switch (bits) {
          case 32:
            // Can't be "out of range", just loses precision
            cast(float*, data)[n] = cast(float, d);
            return;

          case 64:
            cast(double*, data)[n] = d;
            return;
        }
    }
    else {
        int64_t i;
        if (IS_INTEGER(v))
            i = VAL_INT64(v);
        else if (IS_DECIMAL(v))
            i = cast(int32_t, VAL_DECIMAL(v));
        else
            fail (Error_Invalid_Core(v, specifier));

        if (sign) {
            switch (bits) {
              case 8:
                if (i < INT8_MIN or i > INT8_MAX)
                    goto out_of_range;
                cast(int8_t*, data)[n] = cast(int8_t, i);
                return;

              case 16:
                if (i < INT16_MIN or i > INT16_MAX)
                    goto out_of_range;
                cast(int16_t*, data)[n] = cast(int16_t, i);
                return;

              case 32:
                if (i < INT32_MIN or i > INT32_MAX)
                    goto out_of_range;
                cast(int32_t*, data)[n] = cast(int32_t, i);
                return;

              case 64:
                // type uses full range
                cast(int64_t*, data)[n] = i;
                return;
            }
        }
        else { // unsigned
            if (i < 0)
                goto out_of_range;

            switch (bits) {
            case 8:
                if (i > UINT8_MAX)
                    goto out_of_range;
                cast(uint8_t*, data)[n] = cast(uint8_t, i);
                return;

            case 16:
                if (i > UINT16_MAX)
                    goto out_of_range;
                cast(uint16_t*, data)[n] = cast(uint16_t, i);
                return;

            case 32:
                if (i > UINT32_MAX)
                    goto out_of_range;
                cast(uint32_t*, data)[n] = cast(uint32_t, i);
                return;

            case 64:
                // outside of being negative, uses full range
                cast(int64_t*, data)[n] = i;
                return;
            }
        }
    }

    panic ("Unsupported vector element sign/type/size combination");

  out_of_range:;

    rebJumps(
        "FAIL [",
            v, "{out of range for} unspaced [", rebI(bits), "{-bit}]",
            rebT(sign ? "signed" : "unsigned"), "{VECTOR! type}"
        "]"
    );
}

INLINE void Set_Vector_At(
    REBSER *series,
    REBLEN index,
    const Value* v
){
    Set_Vector_At_Core(series, index, v, SPECIFIED);
}

void Set_Vector_Row(REBSER *ser, const Value* blk) // !!! can not be BLOCK!?
{
    REBLEN idx = VAL_INDEX(blk);
    REBLEN len = VAL_LEN_AT(blk);

    if (IS_BLOCK(blk)) {
        Cell* val = VAL_ARRAY_AT(blk);

        REBLEN n = 0;
        for (; NOT_END(val); val++) {
            //if (n >= ser->tail) Expand_Vector(ser);

            Set_Vector_At_Core(ser, n++, val, VAL_SPECIFIER(blk));
        }
    }
    else { // !!! This would just interpet the data as int64_t pointers (???)
        REBYTE *data = VAL_BIN_AT(blk);

        DECLARE_LOCAL (temp);

        REBLEN n = 0;
        for (; len > 0; len--, idx++) {
            Init_Integer(temp, cast(REBI64, data[idx]));
            Set_Vector_At(ser, n++, temp);
        }
    }
}


//
//  Vector_To_Array: C
//
// Convert a vector to a block.
//
REBARR *Vector_To_Array(const Value* vect)
{
    REBSER *ser = VAL_SERIES(vect);
    REBLEN len = VAL_LEN_AT(vect);
    if (len <= 0)
        fail (Error_Invalid(vect));

    REBARR *arr = Make_Arr(len);
    Cell* dest = ARR_HEAD(arr);
    REBLEN n;
    for (n = VAL_INDEX(vect); n < VAL_LEN_HEAD(vect); ++n, ++dest)
        Get_Vector_At(dest, ser, n);

    TERM_ARRAY_LEN(arr, len);
    assert(IS_END(dest));

    return arr;
}


//
//  Compare_Vector: C
//
// !!! Comparison in R3-Alpha was an area that was not well developed.  This
// routine builds upon Compare_Modify_Values(), which does not discern > and
// <, however the REBINT returned here is supposed to.  Review if this code
// ever becomes relevant.
//
REBINT Compare_Vector(const Cell* v1, const Cell* v2)
{
    REBSER *ser1 = VAL_SERIES(v1);
    REBSER *ser2 = VAL_SERIES(v2);

    bool non_integer1 = (MISC(ser1).vect_info.non_integer == 1);
    bool non_integer2 = (MISC(ser2).vect_info.non_integer == 1);
    if (non_integer1 != non_integer2)
        fail (Error_Not_Same_Type_Raw()); // !!! is this error necessary?

    REBLEN l1 = VAL_LEN_AT(v1);
    REBLEN l2 = VAL_LEN_AT(v2);
    REBLEN len = MIN(l1, l2);

    DECLARE_LOCAL(temp1);
    DECLARE_LOCAL(temp2);
    Init_Integer(temp1, 0);
    Init_Integer(temp2, 0);

    REBLEN n;
    for (n = 0; n < len; n++) {
        Get_Vector_At(temp1, ser1, n + VAL_INDEX(v1));
        Get_Vector_At(temp2, ser2, n + VAL_INDEX(v2));
        if (not Compare_Modify_Values(temp1, temp2, 1)) // strict equality
            return 1; // arbitrary (compare didn't discern > or <)
    }

    return l1 - l2;
}


//
//  Shuffle_Vector: C
//
// !!! R3-Alpha code did this shuffle via the bits in the vector, not by
// extracting into values.  This could use REBYTE* access to get a similar
// effect if it were a priority.  Extract and reinsert REBVALs for now.
//
void Shuffle_Vector(Value* vect, bool secure)
{
    REBSER *ser = VAL_SERIES(vect);
    REBLEN idx = VAL_INDEX(vect);

    DECLARE_LOCAL(temp1);
    DECLARE_LOCAL(temp2);

    REBLEN n;
    for (n = VAL_LEN_AT(vect); n > 1;) {
        REBLEN k = idx + cast(REBLEN, Random_Int(secure)) % n;
        n--;

        Get_Vector_At(temp1, ser, k);
        Get_Vector_At(temp2, ser, n + idx);

        Set_Vector_At(ser, k, temp2);
        Set_Vector_At(ser, n + idx, temp1);
    }
}


//
//  Make_Vector: C
//
static REBSER *Make_Vector(
    bool non_integer, // if true, it's a float/decimal, not integral
    bool sign, // signed or unsigned
    REBINT dims, // number of dimensions
    REBLEN bits, // number of bits per unit (8, 16, 32, 64)
    REBINT len
){
    assert(dims == 1);
    UNUSED(dims);

    if (len > 0x7fffffff)
        fail ("vector size too big");

    REBSER *s = Make_Ser_Core(len + 1, bits / 8, SERIES_FLAG_POWER_OF_2);
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
bool Make_Vector_Spec(Value* out, const Cell* head, REBSPC *specifier)
{
    const Cell* item = head;

    if (specifier) {
        //
        // The specifier would be needed if variables were going to be looked
        // up, but isn't required for just symbol comparisons or extracting
        // integer values.
    }

    bool sign;
    if (IS_WORD(item) && Cell_Word_Id(item) == SYM_UNSIGNED) {
        sign = false;
        ++item;
    }
    else
        sign = true; // default to signed, not unsigned

    bool non_integer;
    if (IS_WORD(item)) {
        if (Cell_Word_Id(item) == SYM_FROM_KIND(REB_INTEGER))
            non_integer = false;
        else if (
            Cell_Word_Id(item) == SYM_FROM_KIND(REB_DECIMAL)
        ){
            non_integer = true;
            if (not sign)
                return false; // C doesn't have unsigned floating points
        }
        else
            return false;
        ++item;
    }
    else
        non_integer = false; // default to integer, not floating point

    REBLEN bits;
    if (not IS_INTEGER(item))
        return false; // bit size required, no defaulting

    bits = Int32(item);
    ++item;

    if (non_integer && (bits == 8 || bits == 16))
        return false; // C doesn't have 8 or 16 bit floating points

    if (not (bits == 8 or bits == 16 or bits == 32 or bits == 64))
        return false;

    REBLEN size;
    if (NOT_END(item) && IS_INTEGER(item)) {
        if (Int32(item) < 0)
            return false;
        size = Int32(item);
        ++item;
    }
    else
        size = 1; // !!! default size to 1 (?)

    // Initial data:

    const Value* iblk;
    if (NOT_END(item) and (IS_BLOCK(item) or IS_BINARY(item))) {
        REBLEN len = VAL_LEN_AT(item);
        if (IS_BINARY(item) and not non_integer)
            return false;
        if (len > size)
            size = len;
        iblk = KNOWN(item);
        ++item;
    }
    else
        iblk = NULL;

    REBLEN index;
    if (NOT_END(item) && IS_INTEGER(item)) {
        index = (Int32s(item, 1) - 1);
        ++item;
    }
    else
        index = 0; // default index offset inside returned cell to 0

    if (NOT_END(item))
        return false;

    // !!! Dims appears to be part of unfinished work on multidimensional
    // vectors, which along with the rest of this should be storing in a
    // OBJECT!-like structure for a user-defined type, vs being bit-packed.
    //
    REBINT dims = 1;

    REBSER *vect = Make_Vector(non_integer, sign, dims, bits, size);
    if (not vect)
        return false;

    if (iblk != NULL)
        Set_Vector_Row(vect, iblk);

    Init_Any_Series_At(out, REB_VECTOR, vect, index);
    return true;
}


//
//  MAKE_Vector: C
//
REB_R MAKE_Vector(Value* out, enum Reb_Kind kind, const Value* arg)
{
    // CASE: make vector! 100
    if (IS_INTEGER(arg) || IS_DECIMAL(arg)) {
        REBINT size = Int32s(arg, 0);
        if (size < 0)
            goto bad_make;

        const bool non_integer = false;
        const bool sign = true;
        const REBINT dims = 1;
        REBSER *ser = Make_Vector(non_integer, sign, dims, 32, size);
        return Init_Vector(out, ser);
    }

    return TO_Vector(out, kind, arg);

  bad_make:;
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Vector: C
//
REB_R TO_Vector(Value* out, enum Reb_Kind kind, const Value* arg)
{
    if (IS_BLOCK(arg)) {
        if (Make_Vector_Spec(out, VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg)))
            return out;
    }
    fail (Error_Bad_Make(kind, arg));
}


//
//  CT_Vector: C
//
REBINT CT_Vector(const Cell* a, const Cell* b, REBINT mode)
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
void Pick_Vector(Value* out, const Value* value, const Value* picker) {
    REBSER *vect = VAL_SERIES(value);

    REBINT n;
    if (IS_INTEGER(picker) or IS_DECIMAL(picker)) // #2312
        n = Int32(picker);
    else
        fail (Error_Invalid(picker));

    if (n == 0) {
        Init_Nulled(out);
        return; // Rebol2/Red convention, 0 is bad pick
    }

    if (n < 0)
        ++n; // Rebol/Red convention, picking -1 from tail gives last item

    n += VAL_INDEX(value);

    if (n <= 0 or cast(REBLEN, n) > SER_LEN(vect)) {
        Init_Nulled(out);
        return; // out of range of vector data
    }

    Get_Vector_At(out, vect, n - 1);
}


//
//  Poke_Vector_Fail_If_Read_Only: C
//
void Poke_Vector_Fail_If_Read_Only(
    Value* value,
    const Value* picker,
    const Value* poke
) {
    REBSER *vect = VAL_SERIES(value);
    FAIL_IF_READ_ONLY_SERIES(vect);

    REBINT n;
    if (IS_INTEGER(picker) or IS_DECIMAL(picker)) // #2312
        n = Int32(picker);
    else
        fail (Error_Invalid(picker));

    if (n == 0)
        fail (Error_Out_Of_Range(picker)); // Rebol2/Red convention
    if (n < 0)
        ++n; // Rebol2/Red convention, poking -1 from tail sets last item

    n += VAL_INDEX(value);

    if (n <= 0 or cast(REBLEN, n) > SER_LEN(vect))
        fail (Error_Out_Of_Range(picker));

    Set_Vector_At(vect, n - 1, poke);
}


//
//  PD_Vector: C
//
// Path dispatch acts like PICK for GET-PATH! and POKE for SET-PATH!
//
REB_R PD_Vector(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    if (opt_setval) {
        Poke_Vector_Fail_If_Read_Only(pvs->out, picker, opt_setval);
        return R_INVISIBLE;
    }

    Pick_Vector(pvs->out, pvs->out, picker);
    return pvs->out;
}


//
//  REBTYPE: C
//
REBTYPE(Vector)
{
    Value* value = D_ARG(1);
    REBSER *ser;

    // Common operations for any series type (length, head, etc.)
    //
    REB_R r = Series_Common_Action_Maybe_Unhandled(frame_, verb);
    if (r != R_UNHANDLED)
        return r;

    REBSER *vect = VAL_SERIES(value);

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        Option(SymId) property = Cell_Word_Id(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            //bits = 1 << (vect->size & 3);
            return Init_Integer(D_OUT, SER_LEN(vect));

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

        ser = Copy_Sequence_Core(vect, NODE_FLAG_MANAGED);
        MISC(ser).vect_info = MISC(vect).vect_info; // attributes
        Init_Vector(D_OUT, ser);
        return D_OUT; }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;
        UNUSED(PAR(value));

        FAIL_IF_READ_ONLY_SERIES(vect);

        if (REF(seed) || REF(only))
            fail (Error_Bad_Refines_Raw());

        Shuffle_Vector(D_ARG(1), REF(secure));
        return D_ARG(1); }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), verb));
}


//
//  MF_Vector: C
//
void MF_Vector(REB_MOLD *mo, const Cell* v, bool form)
{
    REBSER *vect = VAL_SERIES(v);

    REBLEN len;
    REBLEN n;
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
        len = VAL_LEN_HEAD(v);
        n = 0;
    } else {
        len = VAL_LEN_AT(v);
        n = VAL_INDEX(v);
    }

    bool non_integer = (MISC(vect).vect_info.non_integer == 1);
    bool sign = (MISC(vect).vect_info.sign == 1);
    REBLEN bits = MISC(vect).vect_info.bits;

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

    DECLARE_LOCAL (temp);

    REBLEN c = 0;
    for (; n < SER_LEN(vect); n++) {

        Get_Vector_At(temp, vect, n);

        REBYTE buf[32];
        REBYTE l;
        if (non_integer)
            l = Emit_Decimal(buf, VAL_DECIMAL(temp), 0, '.', mo->digits);
        else
            l = Emit_Integer(buf, VAL_INT64(temp));
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
        TERM_BIN_LEN(mo->series, BIN_LEN(mo->series) - 1);
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
