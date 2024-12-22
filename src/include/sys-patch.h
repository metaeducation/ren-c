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
// Make_Use_Core: C
//
// Handles linking a "USE" stub into the binding chain.  Some contexts have a
// ->next pointer available in them which they can use without a separate
// allocation, but if that pointer is already occupied then a Use stub has to
// be created to give it a place to put another chain's next pointer.
//
INLINE Use* Make_Use_Core(
    const Element* defs,  // must be a context or a WORD!
    Context* parent,
    Flags note
){
    assert(note == CELL_MASK_ERASED_0 or note == CELL_FLAG_USE_NOTE_SET_WORDS);

    Stub* use = Make_Untracked_Stub(STUB_MASK_USE);

    assert(Any_Context(defs) or Is_Word(defs));
    if (Is_Frame(defs))
        assert(Is_Stub_Varlist(Cell_Frame_Phase(defs)));
    Copy_Cell(Stub_Cell(use), defs);

    if (note)
        Stub_Cell(use)->header.bits |= note;

    Tweak_Link_Inherit_Bind(use, parent);
    Corrupt_Unused_Field(use->misc.corrupt);
    Corrupt_Unused_Field(use->info.corrupt);

    return use;
}
