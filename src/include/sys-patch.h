//
//  File: %sys-patch.h
//  Summary: "Definitions for Virtual Binding Patches"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020-2021 Ren-C Open Source Contributors
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
// Virtual Binding patches are small singular arrays which form linked lists
// of contexts.  Patches are in priority order, so that if a word is found in
// the head patch it will resolve there instead of later in the list.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//



#if NO_RUNTIME_CHECKS
    #define Cell_List_Binding(v) \
        Cell_Binding(v)
#else
    INLINE Context* Cell_List_Binding(const Cell* v) {
        assert(Listlike_Cell(v));
        Context* c = Cell_Binding(v);
        if (not c)
            return SPECIFIED;

        Flavor flavor = Stub_Flavor(c);
        assert(
            flavor == FLAVOR_LET
            or flavor == FLAVOR_USE
            or flavor == FLAVOR_VARLIST
            or flavor == FLAVOR_SEA
        );
        return c;
    }
#endif


//
// Alloc_Use_Inherits_Core: C
//
// Handles linking a "USE" stub into a binding chain.  Caller must fill in
// the Stub_Cell() of the resulting Use with a valid ANY-CONTEXT!, or WORD!
// bound into a context.
//
// Note that sometimes a VarList or SeaOfVars have Link_Inherits_Bind()
// pointers available in them which they can use without a separate
// allocation.  But if that pointer is already occupied then a Use stub has
// to be created as a holder to give it a place to put in another chain.
//
INLINE Use* Alloc_Use_Inherits_Core(
    Flags flags,
    Context* inherit
){
    assert(0 == (flags & ~(USE_FLAG_SET_WORDS_ONLY)));

    Stub* use = Make_Untracked_Stub(STUB_MASK_USE | flags);
    Tweak_Link_Inherit_Bind(use, inherit);
    Corrupt_Unused_Field(use->misc.corrupt);
    Corrupt_Unused_Field(use->info.corrupt);

    return use;
}

#define Alloc_Use_Inherits(inherit) \
    Alloc_Use_Inherits_Core(STUB_MASK_0, (inherit))
