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
// Rather than contain a context, each patch contains a WORD! bound to the
// context it refers to.  The word is the last word in the context at the
// time the patch was created.  This allows a virtual binding to rigorously
// capture the size of the object at the time of its creation--which means
// that a cached property indicating whether a lookup in that patch succeeded
// or not can be trusted.
//
// As an added benefit to using a WORD!, the slot where virtual bind caches
// are stored can be used to cleanly keep a link to the next patch in the
// chain.  Further, there's benefit in that the type of the word can be used
// to indicate if the virtual binding is to all words, just SET-WORD!s, or
// other similar rules.
//
// Whenever possible, one wants to create the same virtual binding chain for
// the same object (or pattern of objects).  Not only does that cut down on
// load for the GC, it also means that it's more likely that a cache lookup
// in a word can be reused.  So the LINK() field of a patch is used to make
// a list of "Variants" of a patch with a different "NextLet".
//
// Being able to find if there are any existing variants for a context when
// all you have in hand is a context is important.  Rather than make a global
// table mapping contexts to patches, the contexts use their MISC() field
// to link a variant.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Sharing the MISC() field of a context with the meta information is not
//   optimal, as it means the MISC() field of *every* patch has to be given
//   up for a potential meta.  It also means that one patch becomes permanent.
//


#ifdef NDEBUG
    #define Cell_Specifier(v) \
        BINDING(v)
#else
    INLINE Specifier* Cell_Specifier(const Cell* v) {
        Stub* s = BINDING(v);
        if (not s)
            return SPECIFIED;

        if (IS_LET(s) or IS_USE(s))
            return s;  // virtual bind

        assert(
            CTX_TYPE(cast(Context*, s)) == REB_FRAME
            or CTX_TYPE(cast(Context*, s)) == REB_MODULE
        );
        return cast(Specifier*, s);
    }
#endif


//
// Make_Use_Core: C
//
// Handles linking a "USE" stub into the specifier chain.  Some specifiers
// have a ->next pointer available in them which they can use without a
// separate allocation, but if that pointer is already occupied then a
// USE stub has to be created to give it a place to put another chain's
// next pointer.
//
// 1. It's possible for a user to try and doubly virtual bind things...but
//    for the moment assume it only happens on accident and alert us to it.
//    Over the long run, this needs to be legal, though.
//
// 2. INODE is not used yet (likely application: symbol for patches that
//    represent lets).  Consider uses in patches that represent objects.  So
//    no FLEX_FLAG_INFO_NODE_NEEDS_MARK yet.
//
// 3. MISC is a node, but it's used for linking patches to variants with
//    different chains underneath them...and shouldn't keep that alternate
//    version alive.  So no FLEX_FLAG_MISC_NODE_NEEDS_MARK.
//
// 4. There's currently no way to ask for the "binding of" a LET and get an
//    answer for what the context is.  It's a free-floating stub that you
//    can't pass as the Varlist to Init_Any_Context().  So the only way to
//    refer to it in a cell--a way that the GC keeps it alive--is to refer
//    to it via a WORD!.  This all needs review, but it's what we do for now.
//
// 5. The way it is designed, the list of use/lets terminates in either a
//    nullptr or a context pointer that represents the specifying frame for
//    the chain.  So we can simply point to the existing specifier...whether
//    it is a use, a let, a frame context, or nullptr.
//
// 6. In the past, "Variant" was a circularly linked list of variations of this
//    USE with different NextVirtual() data.  The idea was to assist in
//    avoiding creating unnecessary duplicate chains.  Decay_Flex() would
//    remove patches from the list during GC.  But see the notes on the
//    Variant definition for why it was removed.
//
INLINE Stub* Make_Use_Core(
    const Element* defs,  // must be a context or a WORD!
    Specifier* next,
    Heart affected
){
    Stub* use = Alloc_Singular(
        FLAG_FLAVOR(USE)
            | NODE_FLAG_MANAGED
            | FLEX_FLAG_LINK_NODE_NEEDS_MARK
            /* FLEX_FLAG_INFO_NODE_NEEDS_MARK */  // inode not yet used [2]
            /* FLEX_FLAG_MISC_NODE_NEEDS_MARK */  // node, but not marked [3]
    );

    assert(Any_Context(defs) or Is_Word(defs));
    Copy_Cell(Stub_Cell(use), defs);

    if (affected == REB_SET_WORD)
        Set_Cell_Flag(Stub_Cell(use), USE_NOTE_SET_WORDS);
    else
        assert(affected == REB_WORD);

    LINK(NextUse, use) = next;  // may be use, let, frame context, nullptr [5]
    MISC(Variant, use) = nullptr;  // "Variant" feature removed for now [6]
    INODE(UseReserved, use) = nullptr;  // no application yet [2]

    return use;
}
