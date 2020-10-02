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
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// See %extensions/vector/README.md
//

#include "sys-core.h"

#include "sys-vector.h"


//
//  Get_Vector_At: C
//
// Ren-C vectors are built on type of BINARY!.  This means that the memory
// must be read via memcpy() in order to avoid strict aliasing violations.
//
REBVAL *Get_Vector_At(RELVAL *out, REBCEL(const*) vec, REBLEN n)
{
    REBYTE *data = VAL_VECTOR_HEAD(vec);

    bool integral = VAL_VECTOR_INTEGRAL(vec);
    bool sign = VAL_VECTOR_SIGN(vec);
    REBYTE bitsize = VAL_VECTOR_BITSIZE(vec);

    if (not integral) {
        switch (bitsize) {
          case 32: {
            float f;
            memcpy(&f, cast(float*, data) + n, sizeof(f));
            return Init_Decimal(out, f); }

          case 64: {
            double d;
            memcpy(&d, cast(double*, data) + n, sizeof(d));
            return Init_Decimal(out, d); }
        }
    }
    else {
        if (sign) {
            switch (bitsize) {
              case 8: {
                int8_t i;
                memcpy(&i, cast(int8_t*, data) + n, sizeof(i));
                return Init_Integer(out, i); }

              case 16: {
                int16_t i;
                memcpy(&i, cast(int16_t*, data) + n, sizeof(i));
                return Init_Integer(out, i); }

              case 32: {
                int32_t i;
                memcpy(&i, cast(int32_t*, data) + n, sizeof(i));
                return Init_Integer(out, i); }

              case 64: {
                int64_t i;
                memcpy(&i, cast(int64_t*, data) + n, sizeof(i));
                return Init_Integer(out, i); }
            }
        }
        else {
            switch (bitsize) {
              case 8: {
                uint8_t i;
                memcpy(&i, cast(uint8_t*, data) + n, sizeof(i));
                return Init_Integer(out, i); }

              case 16: {
                uint16_t i;
                memcpy(&i, cast(uint16_t*, data) + n, sizeof(i));
                return Init_Integer(out, i); }

              case 32: {
                uint32_t i;
                memcpy(&i, cast(uint32_t*, data) + n, sizeof(i));
                return Init_Integer(out, i); }

              case 64: {
                int64_t i;
                memcpy(&i, cast(int64_t*, data) + n, sizeof(i));
                if (i < 0)
                    fail ("64-bit integer out of range for INTEGER!");

                return Init_Integer(out, i); }
            }
        }
    }

    panic ("Unsupported vector element sign/type/size combination");
}


static void Set_Vector_At(REBCEL(const*) vec, REBLEN n, const RELVAL *set) {
    assert(IS_INTEGER(set) or IS_DECIMAL(set));  // caller should error

    REBYTE *data = VAL_VECTOR_HEAD(vec);

    bool integral = VAL_VECTOR_INTEGRAL(vec);
    bool sign = VAL_VECTOR_SIGN(vec);
    REBYTE bitsize = VAL_VECTOR_BITSIZE(vec);

    if (not integral) {
        REBDEC d64;
        if (IS_INTEGER(set))
            d64 = cast(double, VAL_INT64(set));
        else {
            assert(IS_DECIMAL(set));
            d64 = VAL_DECIMAL(set);
        }

        switch (bitsize) {
          case 32: {
            // Can't be "out of range", just loses precision
            REBD32 d = cast(REBD32, d64);
            memcpy(cast(REBD32*, data) + n, &d, sizeof(d));
            return; }

          case 64: {
            memcpy(cast(REBDEC*, data) + n, &d64, sizeof(d64));
            return; }
        }
    }
    else {
        int64_t i64;
        if (IS_INTEGER(set))
            i64 = VAL_INT64(set);
        else {
            assert(IS_DECIMAL(set));
            i64 = cast(int32_t, VAL_DECIMAL(set));
        }

        if (sign) {
            switch (bitsize) {
              case 8: {
                if (i64 < INT8_MIN or i64 > INT8_MAX)
                    goto out_of_range;
                int8_t i = cast(int8_t, i64);
                memcpy(cast(int8_t*, data) + n, &i, sizeof(i));
                return; }

              case 16: {
                if (i64 < INT16_MIN or i64 > INT16_MAX)
                    goto out_of_range;
                int16_t i = cast(int16_t, i64);
                memcpy(cast(int16_t*, data) + n, &i, sizeof(i));
                return; }

              case 32: {
                if (i64 < INT32_MIN or i64 > INT32_MAX)
                    goto out_of_range;
                int32_t i = cast(int32_t, i64);
                memcpy(cast(int32_t*, data) + n, &i, sizeof(i));
                return; }

              case 64: {
                // type uses full range
                memcpy(cast(int64_t*, data) + n, &i64, sizeof(i64));
                return; }
            }
        }
        else {  // unsigned
            if (i64 < 0)
                goto out_of_range;

            switch (bitsize) {
              case 8: {
                if (i64 > UINT8_MAX)
                    goto out_of_range;
                uint8_t u = cast(uint8_t, i64);
                memcpy(cast(uint8_t*, data) + n, &u, sizeof(u));
                return; }

              case 16: {
                if (i64 > UINT16_MAX)
                    goto out_of_range;
                uint16_t u = cast(uint16_t, i64);
                memcpy(cast(uint16_t*, data) + n, &u, sizeof(u));
                return; }

              case 32: {
                if (i64 > UINT32_MAX)
                    goto out_of_range;
                uint32_t u = cast(uint32_t, i64);
                memcpy(cast(uint32_t*, data) + n, &u, sizeof(u));
                return; }

              case 64: {
                uint32_t u = cast(uint32_t, i64);
                memcpy(cast(uint64_t*, data) + n, &u, sizeof(u));
                return; }
            }
        }
    }

    panic ("Unsupported vector element sign/type/size combination");

  out_of_range:;

    rebJumps(
        "FAIL [",
            SPECIFIC(set), "{out of range for}",
                "unspaced [", rebI(bitsize), "{-bit}]",
                rebT(sign ? "signed" : "unsigned"),
                "{VECTOR! type}"
        "]",
        rebEND
    );
}


void Set_Vector_Row(REBCEL(const*) vec, const REBVAL *blk) // !!! can not be BLOCK!?
{
    REBLEN idx = VAL_INDEX(blk);
    REBLEN len = VAL_LEN_AT(blk);

    if (IS_BLOCK(blk)) {
        const RELVAL *val = VAL_ARRAY_AT(blk);

        REBLEN n = 0;
        for (; NOT_END(val); val++) {
            //if (n >= ser->tail) Expand_Vector(ser);

            Set_Vector_At(vec, n++, val);
        }
    }
    else { // !!! This would just interpet the data as int64_t pointers (???)
        REBYTE *data = VAL_BIN_AT_ENSURE_MUTABLE(blk);

        DECLARE_LOCAL (temp);

        REBLEN n = 0;
        for (; len > 0; len--, idx++) {
            Init_Integer(temp, cast(REBI64, data[idx]));
            Set_Vector_At(vec, n++, temp);
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
    REBLEN len = VAL_LEN_AT(vect);
    if (len <= 0)
        fail (vect);

    REBARR *arr = Make_Array(len);
    RELVAL *dest = ARR_HEAD(arr);
    REBLEN n;
    for (n = VAL_INDEX(vect); n < VAL_LEN_HEAD(vect); ++n, ++dest)
        Get_Vector_At(dest, vect, n);

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
REBINT Compare_Vector(REBCEL(const*) v1, REBCEL(const*) v2)
{
    bool non_integer1 = not VAL_VECTOR_INTEGRAL(v1);
    bool non_integer2 = not VAL_VECTOR_INTEGRAL(v2);
    if (non_integer1 != non_integer2)
        fail (Error_Not_Same_Type_Raw()); // !!! is this error necessary?

    REBLEN l1 = VAL_VECTOR_LEN_AT(v1);
    REBLEN l2 = VAL_VECTOR_LEN_AT(v2);
    REBLEN len = MIN(l1, l2);

    DECLARE_LOCAL(temp1);
    DECLARE_LOCAL(temp2);
    Init_Integer(temp1, 0);
    Init_Integer(temp2, 0);

    REBLEN n;
    for (n = 0; n < len; n++) {
        Get_Vector_At(temp1, v1, n + VAL_VECTOR_INDEX(v1));
        Get_Vector_At(temp2, v2, n + VAL_VECTOR_INDEX(v2));
        const bool strict = true;
        REBINT diff = Compare_Modify_Values(temp1, temp2, strict);
        if (diff != 0)
            return diff;
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
void Shuffle_Vector(REBVAL *vect, bool secure)
{
    REBLEN idx = VAL_VECTOR_INDEX(vect);

    DECLARE_LOCAL(temp1);
    DECLARE_LOCAL(temp2);

    REBLEN n;
    for (n = VAL_VECTOR_LEN_AT(vect); n > 1;) {
        REBLEN k = idx + cast(REBLEN, Random_Int(secure)) % n;
        n--;

        Get_Vector_At(temp1, vect, k);
        Get_Vector_At(temp2, vect, n + idx);

        Set_Vector_At(vect, k, temp2);
        Set_Vector_At(vect, n + idx, temp1);
    }
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
//         datatypes:  integer, decimal
//         dimensions: 1 - N
//         bitsize:    1, 8, 16, 32, 64
//         size:       integer units
//         init:        block of values
//
bool Make_Vector_Spec(REBVAL *out, const RELVAL *head, REBSPC *specifier)
{
    const RELVAL *item = head;

    // The specifier would be needed if variables were going to be looked
    // up, but isn't required for just symbol comparisons or extracting
    // integer values.
    //
    UNUSED(specifier);

    bool sign = true;  // default to signed, not unsigned
    if (IS_WORD(item) and VAL_WORD_SYM(item) == SYM_UNSIGNED) {
        sign = false;
        ++item;
    }

    bool integral = false;  // default to integer, not floating point
    if (not IS_WORD(item))
        return false;
    else {
        if (VAL_WORD_SYM(item) == SYM_INTEGER_X)  // e_X_clamation (INTEGER!)
            integral = true;
        else if (VAL_WORD_SYM(item) == SYM_DECIMAL_X) {  // (DECIMAL!)
            integral = false;
            if (not sign)
                return false;  // C doesn't have unsigned floating points
        }
        else
            return false;
        ++item;
    }

    REBYTE bitsize;
    if (not IS_INTEGER(item))
        return false;  // bit size required, no defaulting
    else {
        REBLEN i = Int32(item);
        if (i == 8 or i == 16) {
            if (not integral)
                return false;  // C doesn't have 8 or 16 bit floating points
        }
        else if (i != 32 and i != 64)
            return false;

        bitsize = i;
        ++item;
    }

    REBYTE len = 1;  // !!! default len to 1...why?
    if (NOT_END(item) && IS_INTEGER(item)) {
        if (Int32(item) < 0)
            return false;
        len = Int32(item);
        ++item;
    }
    else
        len = 1;

    const REBVAL *iblk;
    if (NOT_END(item) and (IS_BLOCK(item) or IS_BINARY(item))) {
        REBLEN init_len = VAL_LEN_AT(item);
        if (IS_BINARY(item) and integral)  // !!! What was this about?
            return false;
        if (init_len > len)  // !!! Expands without error, is this good?
            len = init_len;
        iblk = SPECIFIC(item);
        ++item;
    }
    else
        iblk = NULL;

    // !!! Note: VECTOR! was an ANY-SERIES!.  But as a user-defined type, it
    // is being separated from being the kind of thing that knows how series
    // internals are implemented.  It's not clear that user-defined types
    // like vectors will be positional.  VAL_VECTOR_INDEX() always 0 for now.
    //
    REBLEN index = 0;  // default index offset inside returned REBVAL to 0
    if (NOT_END(item) and IS_INTEGER(item)) {
        index = (Int32s(item, 1) - 1);
        ++item;
    }

    if (NOT_END(item))
        fail ("Too many arguments in MAKE VECTOR! block");

    REBLEN num_bytes = len * (bitsize / 8);
    REBSER *bin = Make_Binary(num_bytes);
    CLEAR(BIN_HEAD(bin), num_bytes);  // !!! 0 bytes -> 0 int/float?
    SET_SERIES_LEN(bin, num_bytes);
    TERM_SERIES(bin);

    Init_Vector(out, bin, sign, integral, bitsize);
    UNUSED(index);  // !!! Not currently used, may (?) be added later

    if (iblk != NULL)
        Set_Vector_Row(out, iblk);

    return true;
}


//
//  TO_Vector: C
//
REB_R TO_Vector(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (IS_BLOCK(arg)) {
        if (Make_Vector_Spec(out, VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg)))
            return out;
    }
    fail (Error_Bad_Make(kind, arg));
}


//
//  MAKE_Vector: C
//
REB_R MAKE_Vector(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_INTEGER(arg) or IS_DECIMAL(arg)) {  // e.g. `make vector! 100`
        REBINT len = Int32s(arg, 0);
        if (len < 0)
            goto bad_make;

        REBYTE bitsize = 32;
        REBLEN num_bytes = (len * bitsize) / 8;
        REBSER *bin = Make_Binary(num_bytes);
        CLEAR(BIN_HEAD(bin), num_bytes);
        SET_SERIES_LEN(bin, num_bytes);
        TERM_SERIES(bin);

        const bool sign = true;
        const bool integral = true;
        return Init_Vector(out, bin, sign, integral, bitsize);
    }

    return TO_Vector(out, kind, arg);

  bad_make:;
    fail (Error_Bad_Make(kind, arg));
}


//
//  CT_Vector: C
//
REBINT CT_Vector(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);
    return Compare_Vector(a, b);
}


//
//  Pick_Vector: C
//
void Pick_Vector(REBVAL *out, const REBVAL *value, const RELVAL *picker) {
    REBINT n;
    if (IS_INTEGER(picker) or IS_DECIMAL(picker)) // #2312
        n = Int32(picker);
    else
        fail (rebUnrelativize(picker));

    if (n == 0) {
        Init_Nulled(out);
        return; // Rebol2/Red convention, 0 is bad pick
    }

    if (n < 0)
        ++n; // Rebol/Red convention, picking -1 from tail gives last item

    n += VAL_VECTOR_INDEX(value);

    if (n <= 0 or cast(REBLEN, n) > VAL_VECTOR_LEN_AT(value)) {
        Init_Nulled(out);
        return; // out of range of vector data
    }

    Get_Vector_At(out, value, n - 1);
}


//
//  Poke_Vector_Fail_If_Read_Only: C
//
void Poke_Vector_Fail_If_Read_Only(
    REBVAL *value,
    const RELVAL *picker,
    const REBVAL *poke
){
    // Because the vector uses Alloc_Pairing() for its 2-cells-of value,
    // it has to defer to the binary itself for locked status (also since it
    // can co-opt a BINARY! as its backing store, it has to honor the
    // protection status of the binary)
    //
    // !!! How does this tie into CONST-ness?  How should aggregate types
    // handle their overall constness vs. that of their components?
    //
    ENSURE_MUTABLE(VAL_VECTOR_BINARY(value));

    REBINT n;
    if (IS_INTEGER(picker) or IS_DECIMAL(picker)) // #2312
        n = Int32(picker);
    else
        fail (rebUnrelativize(picker));

    if (n == 0)
        fail (Error_Out_Of_Range(SPECIFIC(picker)));  // Rebol2/Red convention
    if (n < 0)
        ++n; // Rebol2/Red convention, poking -1 from tail sets last item

    n += VAL_VECTOR_INDEX(value);

    if (n <= 0 or cast(REBLEN, n) > VAL_VECTOR_LEN_AT(value))
        fail (Error_Out_Of_Range(SPECIFIC(picker)));

    Set_Vector_At(value, n - 1, poke);
}


//
//  PD_Vector: C
//
// Path dispatch acts like PICK for GET-PATH! and POKE for SET-PATH!
//
REB_R PD_Vector(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
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
    REBVAL *v = D_ARG(1);

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // same as `v`

        REBSYM property = VAL_WORD_SYM(ARG(property));
        switch (property) {
          case SYM_LENGTH:
            return Init_Integer(D_OUT, VAL_VECTOR_LEN_AT(v));

          default:
            break;
        }

        break; }

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PAR(value));  // same as `v`

        if (REF(part) or REF(deep) or REF(types))
            fail (Error_Bad_Refines_Raw());

        REBBIN *bin = Copy_Series_Core(
            VAL_BINARY(VAL_VECTOR_BINARY(v)),
            NODE_FLAG_MANAGED
        );

        return Init_Vector(
            D_OUT,
            bin,
            VAL_VECTOR_SIGN(v),
            VAL_VECTOR_INTEGRAL(v),
            VAL_VECTOR_BITSIZE(v)
        ); }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;
        UNUSED(PAR(value));

        ENSURE_MUTABLE(VAL_VECTOR_BINARY(v));

        if (REF(seed) or REF(only))
            fail (Error_Bad_Refines_Raw());

        Shuffle_Vector(v, did REF(secure));
        RETURN (v); }

    default:
        break;
    }

    return R_UNHANDLED;
}


//
//  MF_Vector: C
//
void MF_Vector(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    REBLEN len;
    REBLEN n;
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
        len = VAL_VECTOR_LEN_HEAD(v);
        n = 0;
    } else {
        len = VAL_VECTOR_LEN_AT(v);
        n = VAL_VECTOR_INDEX(v);
    }

    bool integral = VAL_VECTOR_INTEGRAL(v);
    bool sign = VAL_VECTOR_SIGN(v);
    REBLEN bits = VAL_VECTOR_BITSIZE(v);

    if (not form) {
        enum Reb_Kind kind = integral ? REB_INTEGER : REB_DECIMAL;
        Pre_Mold(mo, v);
        if (NOT_MOLD_FLAG(mo, MOLD_FLAG_ALL))
            Append_Codepoint(mo->series, '[');

        // `<(opt) unsigned> kind bits len [`
        //
        if (not sign)
            Append_Ascii(mo->series, "unsigned ");
        Append_Spelling(mo->series, Canon(SYM_FROM_KIND(kind)));
        Append_Codepoint(mo->series, ' ');
        Append_Int(mo->series, bits);
        Append_Codepoint(mo->series, len);
        Append_Ascii(mo->series, " [");
        if (len != 0)
            New_Indented_Line(mo);
    }

    DECLARE_LOCAL (temp);

    REBLEN c = 0;
    for (; n < VAL_VECTOR_LEN_AT(v); n++) {

        Get_Vector_At(temp, v, n);

        REBYTE buf[32];
        REBYTE l;
        if (integral)
            l = Emit_Integer(buf, VAL_INT64(temp));
        else
            l = Emit_Decimal(buf, VAL_DECIMAL(temp), 0, '.', mo->digits);
        Append_Ascii_Len(mo->series, s_cast(buf), l);

        if ((++c > 7) && (n + 1 < VAL_VECTOR_LEN_AT(v))) {
            New_Indented_Line(mo);
            c = 0;
        }
        else
            Append_Codepoint(mo->series, ' ');
    }

    // !!! There was some handling here for trimming spaces, should be done
    // another way for UTF-8 everywhere if it's important.

    if (not form) {
        if (len)
            New_Indented_Line(mo);

        Append_Codepoint(mo->series, ']');

        if (NOT_MOLD_FLAG(mo, MOLD_FLAG_ALL))
            Append_Codepoint(mo->series, ']');
        else
            Post_Mold(mo, v);
    }
}
