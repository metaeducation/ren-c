//
//  File: %stub-sea.h
//  Summary: "Sparse Symbol/Value Store Defintions after %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

INLINE SeaOfVars* Info_Patch_Sea(const Stub* patch) {
    assert(Is_Stub_Patch(patch));
    SeaOfVars* sea = cast(SeaOfVars*, INFO_PATCH_SEA(patch));
    assert(CTX_TYPE(sea) == REB_MODULE);
    return sea;
}

INLINE void Tweak_Info_Patch_Sea(Stub* patch, SeaOfVars* sea) {
    assert(Is_Stub_Patch(patch));
    assert(sea != nullptr);
    INFO_PATCH_SEA(patch) = sea;
}


// 1. !!! We need to consider the strictness here, with case sensitive binding
//    we can't be sure it's a match.  :-/  For this moment hope lib doesn't
//    have two-cased variations of anything.
//
INLINE Option(Stub*) Sea_Patch(SeaOfVars* sea, const Symbol* sym, bool strict) {
    if (sea == g_lib_context) {
        Option(SymId) id = Symbol_Id(sym);
        if (id != 0 and id < LIB_SYMS_MAX) {
            if (INFO_PATCH_SEA(&g_lib_patches[id]) == nullptr)  // [1]
                return nullptr;

            return &g_lib_patches[id];
        }
    }

    Symbol* synonym = m_cast(Symbol*, sym);
    do {
        Stub* patch = Misc_Hitch(sym);
        if (Get_Flavor_Flag(SYMBOL, sym, MISC_IS_BIND_STUMP))
            patch = Misc_Hitch(patch);  // skip binding stump

        for (; patch != sym; patch = Misc_Hitch(patch)) {
            if (Info_Patch_Sea(patch) == sea)
                return patch;
        }
        if (strict)
            return nullptr;
        sym = Link_Next_Synonym(sym);
    } while (synonym != sym);

    return nullptr;
}

INLINE Value* Sea_Var(SeaOfVars* sea, const Symbol* sym, bool strict) {
    Stub* patch = maybe Sea_Patch(sea, sym, strict);
    if (not patch)
        return nullptr;
    return cast(Value*, Stub_Cell(patch));
}

INLINE Value* Mutable_Lib_Var(SymId id) {
    assert(id < LIB_SYMS_MAX);
    Value* slot = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    assert(Not_Cell_Flag(slot, PROTECTED));
    return slot;
}

INLINE const Value* Lib_Var(SymId id) {
    assert(id < LIB_SYMS_MAX);
    Value* slot = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    assert(not Is_Nothing(slot));
    return slot;
}

INLINE Sink(Value) Sink_Lib_Var(SymId id) {
    assert(id < LIB_SYMS_MAX);
    return cast(Value*, Stub_Cell(&g_lib_patches[id]));
}

#define LIB(name)  Lib_Var(SYM_##name)

#define SYS_UTIL(name) \
    cast(const Value*, \
        Sea_Var(g_sys_util_context, Canon_Symbol(SYM_##name), true))
