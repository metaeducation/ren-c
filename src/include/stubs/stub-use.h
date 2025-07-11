//
//  file: %stub-use.h
//  summary: "Definitions for Virtual Use Patches"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020-2025 Ren-C Open Source Contributors
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
INLINE Result(Use*) Alloc_Use_Inherits_Core(
    Flags flags,
    Context* inherit
){
    assert(0 == (flags & ~(USE_FLAG_SET_WORDS_ONLY)));

    trap (
      Stub* use = Make_Untracked_Stub(STUB_MASK_USE | flags)
    );
    Tweak_Link_Inherit_Bind_Raw(use, inherit);
    Corrupt_Unused_Field(use->misc.corrupt);
    Corrupt_Unused_Field(use->info.corrupt);

    return u_cast(Use*, use);
}

#define Alloc_Use_Inherits(inherit) \
    Alloc_Use_Inherits_Core(STUB_MASK_0, (inherit))
