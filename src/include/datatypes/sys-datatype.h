//
//  File: %sys-datatype.h
//  Summary: "DATATYPE! Datatype Header"
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
// Rebol2/Red/R3-Alpha have a notion of a distinct DATATYPE! type, which can
// appear in blocks.  However it never really had a reified lexical form, so
// they would default to looking like WORD!s
//
//    r3-alpha>> reduce [integer! block!]
//    == [integer! block!]
//
// You would have to use something like MOLD/ALL to reveal a LOAD-able syntax
// that would get you a DATATYPE! and not a WORD!:
//
//    r3-alpha>> mold/all reduce [integer! block!]
//    == "[#[datatype! integer!] #[datatype! block!]]"
//
// Ren-C's approach is to say datatypes can't be directly represented in a
// block, but rather that they are isotopes...and must be transformed at
// least slightly (through META, REIFY, or otherwise) in order to be put into
// a block.  But those representations need not be uniquely dedicated to
// datatypes, and the lexical types can be applied for other purposes.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * %words.r is arranged so symbols for the fundamental types are at the
//   start of the enumeration.
//
// !!! Consider renaming (or adding a synonym) to just TYPE!
//

inline static bool IS_KIND_SYM(option(SymId) id)
  { return id != 0 and id < cast(SymId, REB_MAX); }

inline static enum Reb_Kind KIND_FROM_SYM(SymId s) {
    assert(IS_KIND_SYM(s));
    return cast(enum Reb_Kind, cast(int, (s)));
}

#define SYM_FROM_KIND(k) \
    cast(SymId, cast(enum Reb_Kind, (k)))


#define VAL_TYPE_SYMBOL(v) \
    SYM(PAYLOAD(Any, (v)).first.node)

#define VAL_TYPE_QUOTEDNESS(v) \
    EXTRA(Datatype, (v)).quotedness

#define INIT_VAL_TYPE_SYMBOL(v,sym) \
    (PAYLOAD(Any, (v)).first.node = (sym))

#define INIT_VAL_TYPE_QUOTEDNESS(v,qbyte) \
    (EXTRA(Datatype, (v)).quotedness = (qbyte))

inline static enum Reb_Kind VAL_TYPE_KIND(noquote(Cell(const*)) v) {
    assert(CELL_HEART(v) == REB_DATATYPE);
    option(SymId) id = ID_OF_SYMBOL(VAL_TYPE_SYMBOL(v));
    assert(unwrap(id) < cast(SymId, REB_MAX));
    return cast(enum Reb_Kind, unwrap(id));
}



// Built in types have their specs initialized from data in the boot block.
// We can quickly find them in the lib context, because the types take up
// the early 64-ish symbol IDs in lib, so just use kind as an index.
//
inline static REBVAL *Init_Builtin_Datatype_Untracked(
    Cell(*) out,
    enum Reb_Kind kind
){
    assert(kind < REB_MAX);
    return Copy_Cell(out, Try_Lib_Var(cast(SymId, kind)));
}

#define Init_Builtin_Datatype(out,kind) \
    TRACK(Init_Builtin_Datatype_Untracked((out), (kind)))


// Custom types have to be registered by extensions.  They are identified by
// a URL, so that there is a way of MAKE-ing them.
//
inline static REBVAL *Init_Datatype_Untracked(
    Cell(*) out,
    Symbol(const*) sym,
    Byte quotedness
){
    assert(quotedness != ISOTOPE_0);  // isotopes have no type

    if (quotedness == UNQUOTED_1) {  // pre-made type may be available
        option(SymId) id = ID_OF_SYMBOL(sym);
        if (id and id < REB_MAX)
            return Init_Builtin_Datatype(out, cast(enum Reb_Kind, unwrap(id)));
    }

    Reset_Unquoted_Header_Untracked(out, CELL_MASK_DATATYPE);
    INIT_VAL_TYPE_SYMBOL(out, sym);
    INIT_VAL_TYPE_QUOTEDNESS(out, quotedness);

    return cast(REBVAL*, out);
}

#define Init_Datatype(out,sym,qbyte) \
    TRACK(Init_Datatype_Untracked((out), (sym), (qbyte)))


// Another table generated from %types.r for builtin typesets
//
extern const REBU64 Typesets[];


// This table is generated from %types.r - the actual table is located in
// %tmp-dispatch.c and linked in only once.
//
// No valid type has a null entry in the table.  Instead there is a hook in
// the slot which will fail if it is ever called.
//
// !!! These used to be const, but the desire to have extension types change
// from being "unhooked" to "hooked" meant they needed to be non-const.  Now
// the only "extension type" which mutates the table is REB_EVENT, so that it
// can be one of the types that encodes its type in a byte.  This lets it
// keep its design goal of fitting an event in a single cell with no outside
// allocations.  The importance of that design goal should be reviewed.
//
extern CFUNC* Builtin_Type_Hooks[REB_MAX][IDX_HOOKS_MAX];


// The datatype only knows a symbol.  Have to look that symbol up to get the
// list of hooks registered by the extension providing the custom type.
//
inline static CFUNC** VAL_TYPE_HOOKS(noquote(Cell(const*)) type) {
    assert(CELL_HEART(type) == REB_DATATYPE);
    enum Reb_Kind k = VAL_TYPE_KIND(type);
    assert(k < REB_MAX);
    return Builtin_Type_Hooks[k];
}

inline static CFUNC** HOOKS_FOR_TYPE_OF(noquote(Cell(const*)) v) {
    enum Reb_Kind k = CELL_HEART(v);
    assert(k < REB_MAX);
    return Builtin_Type_Hooks[k];
}

#define Symbol_Hook_For_Type_Of(v) \
    cast(SYMBOL_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_SYMBOL_HOOK])

#define Generic_Hook_For_Type_Of(v) \
    cast(GENERIC_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_GENERIC_HOOK])

#define Compare_Hook_For_Type_Of(v) \
    cast(COMPARE_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_COMPARE_HOOK])

#define Make_Hook_For_Type(type) \
    cast(MAKE_HOOK*, VAL_TYPE_HOOKS(type)[IDX_MAKE_HOOK])

#define Make_Hook_For_Kind(k) \
    cast(MAKE_HOOK*, Builtin_Type_Hooks[k][IDX_MAKE_HOOK])

#define To_Hook_For_Type(type) \
    cast(TO_HOOK*, VAL_TYPE_HOOKS(type)[IDX_TO_HOOK])

#define Mold_Or_Form_Hook_For_Type_Of(v) \
    cast(MOLD_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_MOLD_HOOK])
