//
//  file: %sys-lib.h
//  summary: "Fast Access for Premade Symbols in the LIB Module"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022-2025 Ren-C Open Source Contributors
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
// For a typical MODULE!, one doesn't know in advance how many entries it
// will have or what their names will be.  So their "Patch" Stubs (each patch
// representing one field) are allocated dynamically at runtime.
//
// (See %struct-sea.h for an explanation of the SeaOfVars design of MODULE!)
//
// But the LIB MODULE! is special, because we know at compile-time what the
// initial values are going to be in it (mostly native ACTION!s).
//
// For all the variables we know are going to be in LIB, we can statically
// allocate space for their Patch Stubs in global memory.  Then, we make it
// easy to get variables by their Symbol*, by choosing small integer SymId
// numbers for all the symbols that appear in LIB...and indexing into the
// static array of Patch with this small integer to find the variable Stub.
//
// Despite this optimization, LIB still permits additional growth with
// dynamic Stubs, for any SymId that is not found in the static array (or
// SYM_0, indicating the name has no SymId abbreviation at all).
//

INLINE Value* Mutable_Lib_Var(SymId id) {
    assert(id <= MAX_SYM_LIB_PREMADE);
    Value* slot = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    assert(Not_Cell_Flag(slot, PROTECTED));
    return slot;
}

INLINE const Value* Lib_Var(SymId id) {
    assert(id <= MAX_SYM_LIB_PREMADE);
    Value* slot = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    assert(not Is_Dual_Unset(slot));
    return slot;
}

INLINE Sink(Value) Sink_Lib_Var(SymId id) {
    assert(id <= MAX_SYM_LIB_PREMADE);
    return cast(Value*, Stub_Cell(&g_lib_patches[id]));
}

#define LIB(name)  Lib_Var(SYM_##name)
