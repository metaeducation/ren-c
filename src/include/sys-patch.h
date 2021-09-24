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
// a list of "Variants" of a patch with a different "NextPatch".
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
    #define SPC(n) \
        cast(REBSPC*, (n)) // makes UNBOUND look like SPECIFIED

    #define VAL_SPECIFIER(v) \
        SPC(BINDING(v))
#else
    inline static REBSPC* SPC(REBNOD *n) {
        assert(n != SPECIFIED); // use SPECIFIED, not SPC(SPECIFIED)

        assert(IS_VARLIST(ARR(n)));

        // Note: May be managed or unamanged.

        return cast(REBSPC*, n);
    }

    inline static REBSPC *VAL_SPECIFIER(REBCEL(const*) v) {
        assert(ANY_ARRAY_KIND(CELL_HEART(v)));

        REBARR *a = ARR(BINDING(v));  // REVIEW: Inaccessible?
        if (not a)
            return SPECIFIED;

        if (IS_PATCH(a))
            return cast(REBSPC*, a);  // virtual bind

        // Historically, while an ANY-WORD! could be bound to any object, an
        // ANY-ARRAY! could only be bound to a frame...which would be needed
        // to resolve relative values.  This was extended by virtual binding,
        // which then further extended to the idea of using modules as scopes
        // that sit at the tail of a virtual binding chain and handle anything
        // that "falls through".
        //
        assert(IS_VARLIST(a));
        return cast(REBSPC*, a);
    }
#endif


inline static REBSPC *NextPatch(REBSPC *specifier) {
    assert(
        SER_FLAVOR(specifier) == FLAVOR_VARLIST
        or SER_FLAVOR(specifier) == FLAVOR_PATCH
    );
    return ARR(node_LINK(NextPatch, specifier));
}


//
//  Virtual_Bind_Patchify: C
//
// Update the binding in an array so that it adds the given context as
// overriding the bindings.  This is done without actually mutating the
// structural content of the array...but means words in the array will need
// additional calculations that take the virtual binding chain into account
// as part of Get_Word_Context().
//
// !!! There might be interesting cases here to build a binder and do some
// kind of preemptive caching on the material.  But for now it just is set
// at the tip of the block and spreads influence a step at a time.
//
inline static void Virtual_Bind_Patchify(
    REBVAL *any_array,
    REBCTX *ctx,
    enum Reb_Kind kind
){
    assert(kind == REB_WORD or kind == REB_SET_WORD);

    REBARR *patch = Alloc_Singular(
        FLAG_FLAVOR(PATCH)
            | NODE_FLAG_MANAGED
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK
            | ((kind == REB_SET_WORD) ? PATCH_FLAG_SET_WORDS_ONLY : 0)
    );

    Copy_Cell(ARR_SINGLE(patch), CTX_ARCHETYPE(ctx));

    mutable_LINK(NextPatch, patch) = VAL_SPECIFIER(any_array);
    mutable_MISC(Variant, patch) = nullptr;  // defunct feature atm.
    mutable_INODE(VbindUnused, patch) = nullptr;

    mutable_BINDING(any_array) = patch;

    // !!! Const was considered a good default.  Should that be reviewed?
    //
    Constify(any_array);
}
