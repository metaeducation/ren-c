//
//  File: %cell-datatype.h
//  Summary: "DATATYPE! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// Rebol2/Red/R3-Alpha have a notion of a distinct DATATYPE! type, which can
// appear in blocks.  However it never really had a reified lexical form, so
// they would default to looking like WORD!s
//
//    r3-alpha>> reduce [integer! block!]
//    == [integer! block!]
//
// You would have to use something like MOLD:ALL to reveal a LOAD-able syntax
// that would get you a DATATYPE! and not a WORD!:
//
//    r3-alpha>> mold:all reduce [integer! block!]
//    == "[#[datatype! integer!] #[datatype! block!]]"
//
// Ren-C's approach is to introduce several lexical types to represent types
// and type constraints.
//
//    >> integer!
//    == &[integer]
//
//    >> kind of 10
//    == &[integer]
//
//    >> match &any-series? [a b c]
//    == [a b c]
//
//    >> match &any-series? 10
//    == ~null~  ; anti
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * %words.r is arranged so symbols for the fundamental types are at the
//   start of the enumeration.
//

INLINE bool Is_Symbol_Id_For_A_Type(SymId id) {
    assert(id != SYM_0);
    return u_cast(SymId16, id) <= u_cast(SymId16, MAX_TYPE_BYTE);
}

INLINE Type Type_From_Symbol_Id(SymId id) {
    assert(Is_Symbol_Id_For_A_Type(id));
    return u_cast(TypeEnum, id);
}

INLINE SymId Symbol_Id_From_Type(Type type) {
    assert(type != TYPE_0);
    return cast(SymId, u_cast(SymId16, u_cast(Byte, type)));
}

INLINE Type Cell_Datatype_Type(const Cell* v) {
    assert(Heart_Of(v) == TYPE_TYPE_BLOCK);
    if (Cell_Series_Len_At(v) != 1)
        fail ("Type blocks only allowed one element for now");
    const Element* item = Cell_List_At(nullptr, v);
    if (not Is_Word(item))
        fail ("Type blocks only allowed WORD! items for now");
    Option(SymId) id = Cell_Word_Id(item);
    if (not id or not Is_Symbol_Id_For_A_Type(unwrap id))
        fail ("Type blocks only allowed builtin types for now");
    return u_cast(TypeEnum, unwrap id);
}

INLINE Heart Cell_Datatype_Heart(const Cell* v) {
    Type t = Cell_Datatype_Type(v);
    assert(t != TYPE_0);
    assert(cast(Byte, t) <= MAX_HEART_BYTE);  // not QUOTED/QUASI/ANTI
    return u_cast(HeartEnum, u_cast(Byte, t));
}

// Ren-C uses TYPE-BLOCK! with WORD! for built in datatypes
//
INLINE Value* Init_Builtin_Datatype_Untracked(
    Init(Element) out,
    Type type
){
    assert(type <= MAX_TYPE);
    Source* a = Alloc_Singular(FLEX_MASK_MANAGED_SOURCE);

    Init_Word(Stub_Cell(a), Canon_Symbol(cast(SymId, type)));
    return Init_Any_List(out, TYPE_TYPE_BLOCK, a);
}

#define Init_Builtin_Datatype(out,type) \
    TRACK(Init_Builtin_Datatype_Untracked((out), (type)))


// Used by the Typechecker intrinsic, but also Generic dispatch and PARAMETER!
// typechecking optimization.
//
INLINE bool Builtin_Typeset_Check(TypesetByte typeset_byte, Type type) {
    TypesetFlags typeset = g_typesets[typeset_byte];

    if (typeset & TYPESET_FLAG_0_RANGE) {  // trivial ranges ok (one datatype)
        Byte start = THIRD_BYTE(&typeset);
        Byte end = FOURTH_BYTE(&typeset);
        return start <= u_cast(Byte, type) and u_cast(Byte, type) <= end;
    }
    else {  // just a typeset flag
        return did (g_sparse_memberships[u_cast(Byte, type)] & typeset);
    }
}
