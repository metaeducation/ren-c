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


#define VAL_TYPE_KIND_ENUM(v) \
    EXTRA(Datatype, (v)).kind

inline static enum Reb_Kind VAL_TYPE_KIND_OR_CUSTOM(noquote(Cell(const*)) v) {
    assert(CELL_HEART(v) == REB_DATATYPE);
    return VAL_TYPE_KIND_ENUM(v);
}

inline static enum Reb_Kind VAL_TYPE_KIND(noquote(Cell(const*)) v) {
    assert(CELL_HEART(v) == REB_DATATYPE);
    enum Reb_Kind k = VAL_TYPE_KIND_ENUM(v);
    assert(k != REB_CUSTOM);
    return k;
}

#define INIT_VAL_TYPE_HOOKS             INIT_VAL_NODE2
#define VAL_TYPE_CUSTOM(v)              BIN(VAL_NODE2(v))


// Built in types have their specs initialized from data in the boot block.
// We can quickly find them in the lib context, because the types take up
// the early 64-ish symbol IDs in lib, so just use kind as an index.
//
inline static REBVAL *Init_Builtin_Datatype(
    Cell(*) out,
    enum Reb_Kind kind
){
    assert(kind < REB_MAX);
    Copy_Cell(out, Datatype_From_Kind(kind));
    assert(VAL_TYPE_KIND(out) == kind);
    assert(Not_Cell_Flag(out, FIRST_IS_NODE));
    assert(Not_Cell_Flag(out, SECOND_IS_NODE));  // only custom types have
    return cast(REBVAL*, out);
}


// Custom types have to be registered by extensions.  They are identified by
// a URL, so that there is a way of MAKE-ing them.
//
inline static REBVAL *Init_Custom_Datatype(
    Cell(*) out,
    const REBTYP *type
){
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_DATATYPE)
            | CELL_FLAG_SECOND_IS_NODE
    );
    VAL_TYPE_KIND_ENUM(out) = REB_CUSTOM;
    INIT_VAL_TYPE_HOOKS(out, type);
    return cast(REBVAL*, out);
}


//=//// TYPE HOOK ACCESS //////////////////////////////////////////////////=//
//
// Built-in types identify themselves as one of ~64 fundamental "kinds".  This
// occupies a byte in the header (64 is chosen as a limit currently in order
// to be used with 64-bit typesets, but this is due for change).
//
// Extension types all use the same builtin-type in their header: REB_CUSTOM.
// However, some bits in the cell must be surrendered in order for the full
// type to be expressed.  They have to sacrifice their "Extra" bits.
//
// For efficiency, what's put in the extra is what would be like that type's
// row in the `Builtin_Type_Hooks` if it had been built-in.  These table
// rows are speculatively implemented as an untyped array of CFUNC* which is
// null terminated (vs. a struct with typed fields) so that the protocol can
// be expanded without breaking strict aliasing.
//

enum Reb_Type_Hook_Index {
    IDX_GENERIC_HOOK,
    IDX_COMPARE_HOOK,
    IDX_MAKE_HOOK,
    IDX_TO_HOOK,
    IDX_MOLD_HOOK,
    IDX_HOOK_NULLPTR,  // see notes on why null termination convention
    IDX_HOOKS_MAX
};


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


inline static CFUNC** VAL_TYPE_HOOKS(noquote(Cell(const*)) type) {
    assert(CELL_HEART(type) == REB_DATATYPE);
    enum Reb_Kind k = VAL_TYPE_KIND_OR_CUSTOM(type);
    if (k != REB_CUSTOM)
        return Builtin_Type_Hooks[k];
    return cast(CFUNC**, m_cast(Byte*, SER_DATA(VAL_TYPE_CUSTOM(type))));
}

inline static CFUNC** HOOKS_FOR_TYPE_OF(noquote(Cell(const*)) v) {
    enum Reb_Kind k = CELL_HEART(v);
    if (k != REB_CUSTOM)
        return Builtin_Type_Hooks[k];
    return cast(CFUNC**, m_cast(Byte*, SER_DATA(CELL_CUSTOM_TYPE(v))));
}

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


// !!! Transitional hack to facilitate construction syntax `#[image! [...]]`
// Whether or not LOAD itself should be able to work with extension types is
// an open question...for now, not ruling out the idea...but the design is
// not there for an "extensible scanner".
//
#define Make_Hook_For_Image() \
    cast(MAKE_HOOK*, \
        VAL_TYPE_HOOKS(ARR_AT(PG_Extension_Types, 1))[IDX_MAKE_HOOK])
