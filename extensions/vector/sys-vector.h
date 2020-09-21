//
//  File: %sys-vector.c
//  Summary: "Vector Datatype header file"
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
// The cell for a REB_VECTOR points to a "pairing"--which is two value cells
// stored in an optimized format that fits inside one REBSER node.  This is
// a relatively light allocation, which allows the vector's properties
// (bit width, signedness, integral-ness) to be stored in addition to a
// BINARY! of the vector's bytes.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * See %extensions/vector/README.md
//

extern REBTYP *EG_Vector_Type;

#define VAL_VECTOR_BINARY(v) \
    VAL(PAYLOAD(Any, (v)).first.node)  // pairing[0]

#define VAL_VECTOR_SIGN_INTEGRAL_WIDE(v) \
    PAIRING_KEY(VAL(PAYLOAD(Any, (v)).first.node))  // pairing[1]

#define VAL_VECTOR_SIGN(v) \
    PAYLOAD(Any, VAL_VECTOR_SIGN_INTEGRAL_WIDE(v)).first.flag

inline static bool VAL_VECTOR_INTEGRAL(REBCEL(const*) v) {
    assert(CELL_CUSTOM_TYPE(v) == EG_Vector_Type);
    REBVAL *siw = VAL_VECTOR_SIGN_INTEGRAL_WIDE(v);
    if (PAYLOAD(Any, siw).second.flag != 0)
        return true;

    assert(VAL_VECTOR_SIGN(v));
    return false;
}

inline static REBYTE VAL_VECTOR_WIDE(REBCEL(const*) v) {  // "wide" REBSER term
    int32_t wide = EXTRA(Any, VAL_VECTOR_SIGN_INTEGRAL_WIDE(v)).i32;
    assert(wide == 1 or wide == 2 or wide == 3 or wide == 4);
    return wide;
}

#define VAL_VECTOR_BITSIZE(v) \
    (VAL_VECTOR_WIDE(v) * 8)

inline static REBYTE *VAL_VECTOR_HEAD(REBCEL(const*) v) {
    assert(CELL_CUSTOM_TYPE(v) == EG_Vector_Type);
    REBVAL *binary = VAL(PAYLOAD(Any, v).first.node);
    return BIN_HEAD(VAL_BINARY_ENSURE_MUTABLE(binary));
}

inline static REBLEN VAL_VECTOR_LEN_AT(REBCEL(const*) v) {
    assert(CELL_CUSTOM_TYPE(v) == EG_Vector_Type);
    return VAL_LEN_HEAD(VAL_VECTOR_BINARY(v)) / VAL_VECTOR_WIDE(v);
}

#define VAL_VECTOR_INDEX(v) 0  // !!! Index not currently supported
#define VAL_VECTOR_LEN_HEAD(v) VAL_VECTOR_LEN_AT(v)

inline static REBVAL *Init_Vector(
    RELVAL *out,
    REBBIN *bin,
    bool sign,
    bool integral,
    REBYTE bitsize
){
    RESET_CUSTOM_CELL(out, EG_Vector_Type, CELL_FLAG_FIRST_IS_NODE);

    REBVAL *paired = Alloc_Pairing();

    Init_Binary(paired, bin);
    assert(BIN_LEN(bin) % (bitsize / 8) == 0);

    REBVAL *siw = RESET_CELL(
        PAIRING_KEY(paired),
        REB_V_SIGN_INTEGRAL_WIDE,
        CELL_MASK_NONE
    );
    mutable_MIRROR_BYTE(siw) = REB_LOGIC;  // fools Is_Bindable()
    assert(bitsize == 8 or bitsize == 16 or bitsize == 32 or bitsize == 64);
    PAYLOAD(Any, siw).first.flag = sign;
    PAYLOAD(Any, siw).second.flag = integral;
    EXTRA(Any, siw).i32 = bitsize / 8;  // e.g. VAL_VECTOR_WIDE()

    Manage_Pairing(paired);
    INIT_VAL_NODE(out, paired);
    return SPECIFIC(out);
}


// !!! These hooks allow the REB_VECTOR cell type to dispatch to code in the
// VECTOR! extension if it is loaded.
//
extern REBINT CT_Vector(REBCEL(const*) a, REBCEL(const*) b, bool strict);
extern REB_R MAKE_Vector(REBVAL *out, enum Reb_Kind kind, const REBVAL *opt_parent, const REBVAL *arg);
extern REB_R TO_Vector(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg);
extern void MF_Vector(REB_MOLD *mo, REBCEL(const*) v, bool form);
extern REBTYPE(Vector);
extern REB_R PD_Vector(REBPVS *pvs, const RELVAL *picker, const REBVAL *opt_setval);
