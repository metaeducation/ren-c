//
//  file: %stub-sea.h
//  summary: "Sparse Symbol/Value Store Defintions after %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021-2024 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %struct-sea.h for an explanation of the SeaOfVars type.
//
// This contains inline functions for looking up variables in modules by
// symbol.  There's a particular optimization for the Lib module, where the
// Patch stubs are contiguously preallocated for built-in Symbols during
// Startup_Lib().
//
// To get a library variable by symbol you can pass the symbol to Lib_Var():
//
//     Lib_Var(SYM_APPEND)
//
// Or use the abbreviated macro LIB(APPEND).  This will directly address the
// cell inside the Patch representing Lib's value of NULL in O(1) time, so
// long as the Symbol was known to the bootstrap process (new symbols will
// be allocated dynamically and linked into Lib's Sea).
//
// All non-Lib SeaOfVars dynamically allocate their Patches, so lookup will
// involve walking a linked list of "Hitch" that are hanging off the Symbol.
// But it's not necessarily slow.  Bound words can hold a binding that points
// directly to the Patch, meaning that search only needs to be done once.
//

INLINE SeaOfVars* Info_Patch_Sea(const Patch* patch) {
    assert(Is_Stub_Patch(patch));
    SeaOfVars* sea = cast(SeaOfVars*, INFO_PATCH_SEA(patch));
    assert(Is_Stub_Sea(sea));
    return sea;
}

INLINE void Tweak_Info_Patch_Sea(Patch* patch, SeaOfVars* sea) {
    assert(Is_Stub_Patch(patch));
    assert(sea != nullptr);
    INFO_PATCH_SEA(patch) = sea;
}


INLINE Option(VarList*) Misc_Sea_Adjunct(SeaOfVars* sea) {
    return cast(VarList*, MISC_SEA_ADJUNCT(sea));
}

INLINE void Tweak_Misc_Sea_Adjunct(
    Stub* sea,
    Option(VarList*) adjunct
){
    assert(Is_Stub_Sea(sea));
    MISC_SEA_ADJUNCT(sea) = maybe adjunct;
    if (adjunct)
        Set_Stub_Flag(sea, MISC_NEEDS_MARK);
    else
        Clear_Stub_Flag(sea, MISC_NEEDS_MARK);
}


INLINE Type Type_From_Symbol_Id(SymId id);

// 1. !!! We need to consider the strictness here, with case sensitive binding
//    we can't be sure it's a match.  :-/  For this moment hope lib doesn't
//    have two-cased variations of anything.
//
INLINE Option(Patch*) Sea_Patch(
    SeaOfVars* sea,
    const Symbol* sym,
    bool strict
){
    if (sea == g_lib_context) {
        Option(SymId) id = Symbol_Id(sym);
        if (id and cast(int, id) <= MAX_SYM_LIB_PREMADE) {
            if (INFO_PATCH_SEA(&g_lib_patches[maybe id]) == nullptr)  // [1]
                return nullptr;

            return &g_lib_patches[maybe id];
        }
    }
    else if (sea == g_datatypes_context) {
        Option(SymId) id = Symbol_Id(sym);
        if (id and (
            cast(int, id) >= MIN_SYM_BUILTIN_TYPES
            and cast(int, id) <= MAX_SYM_BUILTIN_TYPES
         )){
            Type type = Type_From_Symbol_Id(unwrap id);
            assert(
                INFO_PATCH_SEA(&g_datatype_patches[cast(Byte, type)])
                == g_datatypes_context
            );
            return &g_datatype_patches[cast(Byte, type)];
        }
    }

    Symbol* synonym = m_cast(Symbol*, sym);
    do {
        Stub* stub = Misc_Hitch(sym);  // first item may be Stump
        if (Get_Flavor_Flag(SYMBOL, sym, HITCH_IS_BIND_STUMP))
            stub = Misc_Hitch(stub);  // skip binding Stump

        for (; stub != sym; stub = Misc_Hitch(stub)) {  // should be Patch
            if (Info_Patch_Sea(cast(Patch*, stub)) == sea)
                return cast(Patch*, stub);
        }
        if (strict)
            return nullptr;
        sym = Link_Next_Synonym(sym);
    } while (synonym != sym);

    return nullptr;
}

INLINE Option(Slot*) Sea_Slot(SeaOfVars* sea, const Symbol* sym, bool strict) {
    Patch* patch = maybe Sea_Patch(sea, sym, strict);
    if (not patch)
        return nullptr;
    return u_cast(Slot*, Stub_Cell(patch));
}
