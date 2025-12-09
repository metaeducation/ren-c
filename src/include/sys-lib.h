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
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// A. The C code is literally depending on the LIB(<symbol>) slots to do work,
//    e.g. it calls LIB(SET) to get the SET function, and calls it.  If the
//    user were to redefine what LIB/SET was, this would break things.
//    This isn't that different from the Mezzanine depending on what SET is.
//    For this reason, LIB should probably have its fixed definitions frozen
//    after boot.  For the moment that is not done, just because being able
//    to hook and redefine things is useful during development.  It could
//    be a command-line option of some kind to freeze or not...
//

INLINE const Value* Lib_Value(SymId id) {
    assert(id <= MAX_SYM_LIB_PREMADE);
    Value* v = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    if (id != SYM_GHOST)
        assert(not Is_Unsetlike_Ghost(v));
    cant(assert(Get_Cell_Flag(v, PROTECTED)));  // LIB not protected yet [A]
    return v;
}

#define Lib_Stable(id)  Known_Stable(Lib_Value(id))

INLINE Value* Mutable_Lib_Value(SymId id) {  // writing LIB is risky [A]
    assert(id <= MAX_SYM_LIB_PREMADE);
    Value* v = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    assert(Not_Cell_Flag(v, PROTECTED));
    return v;
}

#define Mutable_Lib_Stable(id)  Known_Stable(Mutable_Lib_Value(id))

INLINE Sink(Value) Sink_Lib_Value(SymId id) {
    assert(id <= MAX_SYM_LIB_PREMADE);
    Value* v = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    assert(Is_Unsetlike_Ghost(v));
    assert(Not_Cell_Flag(v, PROTECTED));
    return v;
}

#define LIB(name)  Lib_Stable(SYM_##name)
#define Sink_LIB(name)  Sink_Lib_Value(SYM_##name)
#define Mutable_LIB(name)  Mutable_Lib_Stable(SYM_##name)

#define Protect_LIB(name) /* careful: preprocessor NULL could become SYM_0 */ \
    Protect_Cell(Mutable_Lib_Stable(SYM_##name))  // PROTECT more things [A]
