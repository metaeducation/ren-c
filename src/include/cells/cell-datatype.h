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

INLINE bool IS_KIND_SYM(SymId id) {
    assert(id != SYM_0);
    return u_cast(SymIdNum, id) < u_cast(SymIdNum, REB_MAX);
}

INLINE Kind KIND_FROM_SYM(SymId s) {
    assert(IS_KIND_SYM(s));
    return cast(Kind, s);
}

#define SYM_FROM_KIND(k) \
    cast(SymId, u_cast(SymIdNum, (k)))


#define VAL_TYPE_SYMBOL(v) \
    Cell_Word_Symbol(v)

INLINE Kind VAL_TYPE_KIND(const Cell* v) {
    assert(Cell_Heart(v) == REB_TYPE_BLOCK);
    if (Cell_Series_Len_At(v) != 1)
        fail ("Type blocks only allowed one element for now");
    const Element* item = Cell_List_At(nullptr, v);
    if (not Is_Word(item))
        fail ("Type blocks only allowed WORD! items for now");
    Option(SymId) id = Cell_Word_Id(item);
    if (not id or not IS_KIND_SYM(unwrap id))
        fail ("Type blocks only allowed builtin types for now");
    return cast(Kind, unwrap id);
}

INLINE Heart VAL_TYPE_HEART(const Cell* v) {
    Kind k = VAL_TYPE_KIND(v);
    if (k >= REB_QUASIFORM)
        fail ("Didn't expect QUOTED or QUASIFORM or ANTIFORM for type");
    return cast(Heart, k);
}

// Ren-C uses TYPE-BLOCK! with WORD! for built in datatypes
//
INLINE Value* Init_Builtin_Datatype_Untracked(
    Init(Element) out,
    Kind kind
){
    assert(kind < REB_MAX);
    Source* a = Alloc_Singular(FLEX_MASK_MANAGED_SOURCE);

    Init_Word(Stub_Cell(a), Canon_Symbol(cast(SymId, kind)));
    return Init_Any_List(out, REB_TYPE_BLOCK, a);
}

#define Init_Builtin_Datatype(out,kind) \
    TRACK(Init_Builtin_Datatype_Untracked((out), (kind)))


// Used by the Typechecker intrinsic, but also Generic dispatch and PARAMETER!
// typechecking optimization.
//
INLINE bool Builtin_Typeset_Check(TypesetByte typeset_byte, Kind kind) {
    TypesetFlags typeset = g_typesets[typeset_byte];

    if (typeset & TYPESET_FLAG_0_RANGE) {  // trivial ranges ok (one datatype)
        Byte start = THIRD_BYTE(&typeset);
        Byte end = FOURTH_BYTE(&typeset);
        return start <= kind and kind <= end;
    }

    return did (g_sparse_memberships[kind] & typeset);  // just a typeset flag
}


// This table is generated from %types.r - the actual table is located in
// %tmp-dispatch.c and linked in only once.
//
// No valid type has a null entry in the table.  Instead there is a hook in
// the slot which will fail if it is ever called.
//
// !!! These used to be const, but the desire to have extension types change
// from being "unhooked" to "hooked" meant they needed to be non-const.  The
// importance of that design goal should be reviewed.
//
extern CFunction* Builtin_Type_Hooks[REB_MAX_HEART][IDX_HOOKS_MAX];


INLINE GenericHook* Generic_Hook_For_Heart(Heart h)
  { return cast(GenericHook*, Builtin_Type_Hooks[h][IDX_GENERIC_HOOK]); }

INLINE MoldHook* Mold_Hook_For_Heart(Heart h)
  { return cast(MoldHook*, Builtin_Type_Hooks[h][IDX_MOLD_HOOK]); }
