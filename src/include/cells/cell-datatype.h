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

INLINE bool IS_KIND_SYM(Option(SymId) id)
  { return id != 0 and id < cast(uint16_t, REB_MAX); }

INLINE Kind KIND_FROM_SYM(SymId s) {
    assert(IS_KIND_SYM(s));
    return cast(Kind, s);
}

#define SYM_FROM_KIND(k) \
    cast(SymId, cast(uint16_t, (k)))


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


// Ren-C uses TYPE-BLOCK! with WORD! for built in datatypes
//
INLINE Value* Init_Builtin_Datatype_Untracked(
    Sink(Value*) out,
    Kind kind
){
    assert(kind < REB_MAX);
    Array* a = Alloc_Singular(NODE_FLAG_MANAGED);

    Init_Word(Stub_Cell(a), Canon_Symbol(cast(SymId, kind)));
    return Init_Any_List(out, REB_TYPE_BLOCK, a);
}

#define Init_Builtin_Datatype(out,kind) \
    TRACK(Init_Builtin_Datatype_Untracked((out), (kind)))


#define Typecheck_Atom(tests,atom) \
    Typecheck_Atom_Core((tests), SPECIFIED, (atom))


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
extern CFunction* Builtin_Type_Hooks[REB_MAX][IDX_HOOKS_MAX];


// The datatype only knows a symbol.  Have to look that symbol up to get the
// list of hooks registered by the extension providing the custom type.
//
INLINE CFunction** VAL_TYPE_HOOKS(const Cell* type) {
    Kind k = VAL_TYPE_KIND(type);
    assert(k < REB_MAX);
    return Builtin_Type_Hooks[k];
}

INLINE CFunction** HOOKS_FOR_TYPE_OF(const Cell* v) {
    Heart heart = Cell_Heart(v);
    return Builtin_Type_Hooks[heart];
}

#define Symbol_Hook_For_Type_Of(v) \
    cast(SYMBOL_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_SYMBOL_HOOK])

#define Generic_Hook_For_Type_Of(v) \
    cast(GENERIC_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_GENERIC_HOOK])

#define Compare_Hook_For_Type_Of(v) \
    cast(COMPARE_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_COMPARE_HOOK])

#define Make_Hook_For_Kind(k) \
    cast(MAKE_HOOK*, Builtin_Type_Hooks[k][IDX_MAKE_HOOK])

#define To_Hook_For_Type(type) \
    cast(TO_HOOK*, VAL_TYPE_HOOKS(type)[IDX_TO_HOOK])

#define Mold_Or_Form_Hook_For_Type_Of(v) \
    cast(MOLD_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_MOLD_HOOK])
