//
//  File: %sys-ordered.h
//  Summary: "Order-dependent type macros"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// These macros embed specific knowledge of the type ordering.  Basically any
// changes to %types.r mean having to take into account fixups here.
//
// !!! Review how these might be auto-generated from the table.
//
// !!! There was a historical linkage between the order of types and the
// TOKEN_XXX values.  That might be interesting to exploit for an optimization
// in the future...see notes on the tokens regarding this.
//


//=//// QUOTED ////////////////////////////////////////////////////////////=//
//
// Testing for QUOTED! is special, as it isn't just the REB_QUOTED type, but
// also multiplexed as values > REB_64.  See %sys-quoted.h
//
// !!! Review making this test faster as just `k >= REB_QUOTED` by positioning
// the QUOTED! datatype past all the pseudotypes (e.g. at 63).  This would
// raise REB_MAX, and inflate all the tables for dispatch to 64 items, which
// is not really a big deal...but there are likely other consequences.

inline static bool IS_QUOTED_KIND(REBYTE k)
  { return k == REB_QUOTED or k >= REB_64; }

#define IS_QUOTED(v) \
    IS_QUOTED_KIND(KIND_BYTE(v))


//=//// BINDABILITY ///////////////////////////////////////////////////////=//
//
// Note that an "in-situ" QUOTED! (not a REB_QUOTED kind byte, but using
// larger REB_MAX values) is bindable if the cell it's overlaid into is
// bindable.  It has to handle binding exactly as its contained value.
//
// Actual REB_QUOTEDs (used for higher escape values) have to use a separate
// cell for storage.  The REB_QUOTED type is in the range of enum values that
// report bindability, even if it's storing a type that uses the ->extra field
// for something else.  This is mitigated by putting nullptr in the binding
// field of the REB_QUOTED portion of the cell, instead of mirroring the
// ->extra field of the contained cell...so it comes off as "specified" in
// those cases.
//
// Also note that the MIRROR_BYTE() is what is being tested--e.g. the type
// that the cell payload and extra actually are *for*.  This is what gives
// the CELL_KIND() as opposed to the VAL_TYPE

#define IS_BINDABLE_KIND(k) \
    ((k) >= REB_ISSUE)

#define Is_Bindable(v) \
    IS_BINDABLE_KIND(CELL_KIND_UNCHECKED(v))  // checked elsewhere


//=//// INERTNESS ////////////////////////////////////////////////////////=//
//
// All the inert types are grouped together to make this test fast.
//

inline static bool ANY_INERT_KIND(REBYTE k) {
    assert(k >= REB_BLANK);  // can't call on end/null/void
    return k <= REB_BLOCK;
}

#define ANY_INERT(v) \
    ANY_INERT_KIND(KIND_BYTE(v))

#define ANY_EVALUATIVE(v) \
    (not ANY_INERT_KIND(KIND_BYTE(v)))


//=//// FAST END+VOID+NULL TESTING ////////////////////////////////////////=//
//
// There are many cases where end/void/null all have special handling or need
// to raise errors.  Rather than saying:
//
//     if (IS_END(v)) { fail ("end"); }
//     if (IS_VOID(v)) { fail ("void"); }
//     if (IS_NULL(v)) { fail ("null"); }
//     CommonCaseStuff(v);
//
// This can be collapsed down to one test in the common case, with:
//
//     if (IS_NULLED_OR_VOID_OR_END(v)) {
//        if (IS_END(v)) { fail ("end"); }
//        if (IS_VOID(v)) { fail {"void"); }
//        fail ("null");
//     }
//     CommonCaseStuff(v);

inline static bool IS_NULLED_OR_VOID_KIND(REBYTE k) {
    assert(k != REB_0_END);
    return k <= REB_VOID;
}

#define IS_NULLED_OR_VOID(v) \
    IS_NULLED_OR_VOID_KIND(KIND_BYTE(v))

inline static bool IS_NULLED_OR_VOID_OR_END_KIND(REBYTE k)
    { return k <= REB_VOID; }

#define IS_NULLED_OR_VOID_OR_END(v) \
    IS_NULLED_OR_VOID_OR_END_KIND(KIND_BYTE_UNCHECKED(v))

inline static bool IS_NULLED_OR_BLANK_KIND(REBYTE k)
    { return k == REB_NULLED or k == REB_BLANK; }

#define IS_NULLED_OR_BLANK(v) \
    IS_NULLED_OR_BLANK_KIND(KIND_BYTE(v))


//=//// TYPE CATEGORIES ///////////////////////////////////////////////////=//

#define ANY_VALUE(v) \
    (KIND_BYTE(v) != REB_NULLED)

inline static bool ANY_SCALAR_KIND(REBYTE k)
    { return k >= REB_LOGIC and k <= REB_PAIR; }

#define ANY_SCALAR(v) \
    ANY_SCALAR_KIND(KIND_BYTE(v))

inline static bool ANY_STRING_KIND(REBYTE k)
    { return k >= REB_TEXT and k <= REB_TAG; }

#define ANY_STRING(v) \
    ANY_STRING_KIND(KIND_BYTE(v))

#define ANY_BINSTR_KIND_EVIL_MACRO \
    (k >= REB_BINARY and k <= REB_TAG)


inline static bool ANY_BINSTR_KIND(REBYTE k)
    { return ANY_BINSTR_KIND_EVIL_MACRO; }

#define ANY_BINSTR(v) \
    ANY_BINSTR_KIND(KIND_BYTE(v))


#define ANY_ARRAY_OR_PATH_KIND_EVIL_MACRO \
    (k >= REB_BLOCK and k <= REB_GET_PATH)

inline static bool ANY_ARRAY_OR_PATH_KIND(REBYTE k)
    { return ANY_ARRAY_OR_PATH_KIND_EVIL_MACRO; }

#define ANY_ARRAY_OR_PATH(v) \
    ANY_ARRAY_OR_PATH_KIND(KIND_BYTE(v))


#define ANY_ARRAY_KIND_EVIL_MACRO \
    (k >= REB_BLOCK and k <= REB_GET_GROUP)

inline static bool ANY_ARRAY_KIND(REBYTE k)
    { return ANY_ARRAY_KIND_EVIL_MACRO; }

#define ANY_ARRAY(v) \
    ANY_ARRAY_KIND(KIND_BYTE(v))


#define ANY_SERIES_KIND_EVIL_MACRO \
    (ANY_BINSTR_KIND_EVIL_MACRO or ANY_ARRAY_KIND_EVIL_MACRO)

inline static bool ANY_SERIES_KIND(REBYTE k)
    { return ANY_SERIES_KIND_EVIL_MACRO; }

#define ANY_SERIES(v) \
    ANY_SERIES_KIND(KIND_BYTE(v))


// !!! The ANY-WORD! classification is an odd one, because it's not just
// WORD!/GET-WORD!/SET-WORD! but includes ISSUE!.  Ren-C is looking at avenues
// of attack for this to let strings hold bindings.  To make the ANY_INERT()
// test fast, issue is grouped with the inert types...not the other words.

#define ANY_WORD_KIND_EVIL_MACRO \
    ((k >= REB_WORD and k <= REB_GET_WORD) or k == REB_ISSUE)

inline static bool ANY_WORD_KIND(REBYTE k)
    { return ANY_WORD_KIND_EVIL_MACRO; }

#define ANY_WORD(v) \
    ANY_WORD_KIND(KIND_BYTE(v))

inline static bool ANY_PLAIN_GET_SET_WORD_KIND(REBYTE k)
    { return k >= REB_WORD and k <= REB_GET_WORD; }

#define ANY_PLAIN_GET_SET_WORD(v) \
    ANY_PLAIN_GET_SET_WORD_KIND(KIND_BYTE(v))


#define ANY_PATH_KIND_EVIL_MACRO \
    (k >= REB_PATH and k <= REB_GET_PATH)

inline static bool ANY_PATH_KIND(REBYTE k)
    { return ANY_PATH_KIND_EVIL_MACRO; }

#define ANY_PATH(v) \
    ANY_PATH_KIND(KIND_BYTE(v))


inline static bool ANY_BLOCK_KIND(REBYTE k)
    { return k >= REB_BLOCK and k <= REB_GET_BLOCK; }

#define ANY_BLOCK(v) \
    ANY_BLOCK_KIND(KIND_BYTE(v))


inline static bool ANY_GROUP_KIND(REBYTE k)
    { return k >= REB_GROUP and k <= REB_GET_GROUP; }

#define ANY_GROUP(v) \
    ANY_GROUP_KIND(KIND_BYTE(v))


inline static bool ANY_CONTEXT_KIND(REBYTE k)
    { return k >= REB_OBJECT and k <= REB_PORT; }

#define ANY_CONTEXT(v) \
    ANY_CONTEXT_KIND(KIND_BYTE(v))


inline static bool ANY_NUMBER_KIND(REBYTE k)
    { return k == REB_INTEGER or k == REB_DECIMAL or k == REB_PERCENT; }

#define ANY_NUMBER(v) \
    ANY_NUMBER_KIND(KIND_BYTE(v))


//=//// XXX <=> SET-XXX! <=> GET-XXX! TRANSFORMATION //////////////////////=//
//
// Note that grouping the blocks and paths and words together is more
// important than some property to identify all the GETs/SETs together.

inline static bool ANY_GET_KIND(REBYTE k) {
    return k == REB_GET_WORD or k == REB_GET_PATH
        or k == REB_GET_GROUP or k == REB_GET_BLOCK;
}

inline static bool ANY_SET_KIND(REBYTE k) {
    return k == REB_SET_WORD or k == REB_SET_PATH
        or k == REB_SET_GROUP or k == REB_SET_BLOCK;
}

inline static bool ANY_PLAIN_KIND(REBYTE k) {
    return k == REB_WORD or k == REB_PATH
        or k == REB_GROUP or k == REB_BLOCK;
}

inline static enum Reb_Kind UNGETIFY_ANY_GET_KIND(REBYTE k) {
    assert(ANY_GET_KIND(k));
    return cast(enum Reb_Kind, k - 2);
}

inline static enum Reb_Kind UNSETIFY_ANY_SET_KIND(REBYTE k) {
    assert(ANY_SET_KIND(k));
    return cast(enum Reb_Kind, k - 1);
}

inline static enum Reb_Kind SETIFY_ANY_PLAIN_KIND(REBYTE k) {
    assert(ANY_PLAIN_KIND(k));
    return cast(enum Reb_Kind, k + 1);
}

inline static enum Reb_Kind GETIFY_ANY_PLAIN_KIND(REBYTE k) {
    assert(ANY_PLAIN_KIND(k));
    return cast(enum Reb_Kind, k + 2);
}


//=//// "PARAM" CELLS /////////////////////////////////////////////////////=//
//
// !!! Due to the scarcity of bytes in cells, yet a desire to use them for
// parameters, they are a kind of "container" class in the KIND_BYTE() while
// the actual CELL_KIND (via MIRROR_BYTE()) is a REB_TYPESET.
//
// Making the typeset expression more sophisticated to clearly express a list
// of parameter flags is something planned for the near future.

inline static bool IS_PARAM_KIND(REBYTE k)
    { return k >= REB_P_NORMAL and k <= REB_P_RETURN; }

#define IS_PARAM(v) \
    IS_PARAM_KIND(KIND_BYTE(v))


//=//// TYPE HOOK ACCESS //////////////////////////////////////////////////=//
//
// Built-in types identify themselves as one of 64 fundamental "kinds".  When
// that kind is combined with up to 3 levels of quoting, it uses up a byte
// in the cell's header.  To access behaviors for that type, it is looked
// up in the `Builtin_Type_Hooks` under their index.  Then, the entire rest
// of the cell's bits--the "Payload" and the "Extra"--are available for the
// data portion of the cell.
//
// Extension types all use the same builtin-type in their header: REB_UTYPE.
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
// !!! These used to be const, but the desire to move REB_STRUCT and REB_GOB
// into extensions required the tables to be dynamically modified.  This
// should likely be changed back.
//
extern CFUNC* Builtin_Type_Hooks[REB_MAX][IDX_HOOKS_MAX];

inline static CFUNC *Type_Hooks(
    enum Reb_Type_Hook_Index ihook,
    enum Reb_Kind kind
){
    // For now, only handle builtin types.  Would need to do something
    // special for a UTYPE.
    //
    return Builtin_Type_Hooks[kind][ihook];
}

#define Generic_Hooks(kind) \
    cast(GENERIC_HOOK*, Type_Hooks(IDX_GENERIC_HOOK, (kind)))

#define Path_Hooks(kind) \
    cast(PATH_HOOK*, Type_Hooks(IDX_PATH_HOOK, (kind)))

#define Compare_Hooks(kind) \
    cast(COMPARE_HOOK*, Type_Hooks(IDX_COMPARE_HOOK, (kind)))
    
#define Make_Hooks(kind) \
    cast(MAKE_HOOK*, Type_Hooks(IDX_MAKE_HOOK, (kind)))

#define To_Hooks(kind) \
    cast(TO_HOOK*, Type_Hooks(IDX_TO_HOOK, (kind)))

#define Mold_Or_Form_Hooks(kind) \
    cast(MOLD_HOOK*, Type_Hooks(IDX_MOLD_HOOK, (kind)))
