//
//  File: %sys-datatype.h
//  Summary: "DATATYPE! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: R3-Alpha's notion of a datatype has not been revisited very much in
// Ren-C.  The unimplemented UTYPE! user-defined type concept was removed
// for simplification, pending a broader review of what was needed.
//
// %words.r is arranged so symbols for types are at the start of the enum.
// Note REB_0 is not a type, which lines up with SYM_0 used for symbol IDs as
// "no symbol".  Also, NULL is not a value type, and is at REB_MAX past the
// end of the list.
//
// !!! Consider renaming (or adding a synonym) to just TYPE!
//


#define VAL_TYPE_KIND_ENUM(v) \
    EXTRA(Datatype, (v)).kind

inline static enum Reb_Kind VAL_TYPE_KIND_OR_CUSTOM(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_DATATYPE);
    return VAL_TYPE_KIND_ENUM(v);
}

inline static enum Reb_Kind VAL_TYPE_KIND(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_DATATYPE);
    enum Reb_Kind k = VAL_TYPE_KIND_ENUM(v);
    assert(k != REB_CUSTOM);
    return k;
}

#define VAL_TYPE_SPEC_NODE(v) \
    PAYLOAD(Any, (v)).first.node

#define VAL_TYPE_SPEC(v) \
    ARR(VAL_TYPE_SPEC_NODE(v))

#define VAL_TYPE_HOOKS_NODE(v) \
    PAYLOAD(Any, (v)).second.node

#define VAL_TYPE_CUSTOM(v) \
    SER(VAL_TYPE_HOOKS_NODE(v))


// Built in types have their specs initialized from data in the boot block.
// We can quickly find them in the lib context, because the types take up
// the early 64-ish symbol IDs in lib, so just use kind as an index.
//
inline static REBVAL *Init_Builtin_Datatype(RELVAL *out, enum Reb_Kind kind) {
    assert(kind > REB_0 and kind < REB_MAX);
    Move_Value(out, Datatype_From_Kind(kind));
    assert(GET_CELL_FLAG(out, FIRST_IS_NODE));
    assert(NOT_CELL_FLAG(out, SECOND_IS_NODE));  // only custom types have
    return KNOWN(out);
}


// Custom types have to be registered by extensions.  They are identified by
// a URL, so that there is a way of MAKE-ing them.
//
inline static REBVAL *Init_Custom_Datatype(RELVAL *out, REBTYP *type) {
    RESET_CELL(
        out,
        REB_DATATYPE,
        CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE
    );
    VAL_TYPE_KIND_ENUM(out) = REB_CUSTOM;
    VAL_TYPE_SPEC_NODE(out) = NOD(EMPTY_ARRAY);
    VAL_TYPE_HOOKS_NODE(out) = NOD(type);
    return KNOWN(out);
}


//=//// TYPE HOOK ACCESS //////////////////////////////////////////////////=//
//
// Built-in types identify themselves as one of 64 fundamental "kinds".  When
// that kind is combined with up to 3 levels of quoting, it uses up a byte
// in the cell's header.  To access behaviors for that type, it is looked
// up in the `Builtin_Type_Hooks` under their index.  Then, the entire rest
// of the cell's bits--the "Payload" and the "Extra"--are available for the
// data portion of the cell.
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
    IDX_PATH_HOOK,
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


inline static CFUNC** VAL_TYPE_HOOKS(const RELVAL *type) {
    enum Reb_Kind k = VAL_TYPE_KIND_OR_CUSTOM(type);
    if (k != REB_CUSTOM)
        return Builtin_Type_Hooks[k];
    return cast(CFUNC**, SER_DATA_RAW(VAL_TYPE_CUSTOM(type)));
}

inline static CFUNC** HOOKS_FOR_TYPE_OF(const REBCEL *v) {
    enum Reb_Kind k = CELL_KIND(v);
    if (k != REB_CUSTOM)
        return Builtin_Type_Hooks[k];
    return cast(CFUNC**, SER_DATA_RAW(CELL_CUSTOM_TYPE(v)));
}

#define Generic_Hook_For_Type_Of(v) \
    cast(GENERIC_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_GENERIC_HOOK])

#define Path_Hook_For_Type_Of(v) \
    cast(PATH_HOOK*, HOOKS_FOR_TYPE_OF(v)[IDX_PATH_HOOK])

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
