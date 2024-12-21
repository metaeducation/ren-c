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

        if (Is_Stub_Let(c) or Is_Stub_Use(c))
            return c;  // virtual bind

        assert(Is_Stub_Varlist(c));
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
// 1. It's possible for a user to try and doubly virtual bind things...but
//    for the moment assume it only happens on accident and alert us to it.
//    Over the long run, this needs to be legal, though.
//
// 2. INODE is not used yet (likely application: symbol for patches that
//    represent lets).  Consider uses in patches that represent objects.  So
//    no STUB_FLAG_INFO_NODE_NEEDS_MARK yet.
//
// 3. MISC is a node, but it's used for linking patches to variants with
//    different chains underneath them...and shouldn't keep that alternate
//    version alive.  So no STUB_FLAG_MISC_NODE_NEEDS_MARK.
//
// 4. There's currently no way to ask for the "binding of" a LET and get an
//    answer for what the context is.  It's a free-floating stub that you
//    can't pass as the Varlist to Init_Any_Context().  So the only way to
//    refer to it in a cell--a way that the GC keeps it alive--is to refer
//    to it via a WORD!.  This all needs review, but it's what we do for now.
//
// 5. The way it is designed, the list of use/lets terminates in either a
//    nullptr or a context pointer that represents the specifying frame for
//    the chain.  So we can simply point to the existing binding...whether
//    it is a use, a let, a frame context, or nullptr.
//
// 6. In the past, "Variant" was a circularly linked list of variations of this
//    USE with different Link_Inherit_Bind() data.  The idea was to assist in
//    avoiding creating unnecessary duplicate chains.  Decay_Stub() would
//    remove patches from the list during GC.  But see the notes on the
//    Variant definition for why it was removed.
//
INLINE Use* Make_Use_Core(
    const Element* defs,  // must be a context or a WORD!
    Context* parent,
    Flags note
){
    assert(note == CELL_MASK_ERASED_0 or note == CELL_FLAG_USE_NOTE_SET_WORDS);

    Stub* use = Make_Untracked_Stub(
        FLAG_FLAVOR(USE)
            | NODE_FLAG_MANAGED
            | STUB_FLAG_LINK_NODE_NEEDS_MARK
            /* STUB_FLAG_INFO_NODE_NEEDS_MARK */  // inode not yet used [2]
            /* STUB_FLAG_MISC_NODE_NEEDS_MARK */  // node, but not marked [3]
    );

    assert(Any_Context(defs) or Is_Word(defs));
    if (Is_Frame(defs))
        assert(Is_Stub_Varlist(Cell_Frame_Phase(defs)));
    Copy_Cell(Stub_Cell(use), defs);

    if (note)
        Stub_Cell(use)->header.bits |= note;

    Tweak_Link_Inherit_Bind(use, parent); // use, let, frame context... [5]
    Corrupt_Unused_Field(use->misc.corrupt);  // "Variant" removed for now [6]
    Corrupt_Unused_Field(use->info.corrupt);  // no application yet [2]

    return use;
}
