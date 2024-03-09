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
// Shared routine that handles linking the patch into the context's variant
// list, and bumping the meta out of the misc into the misc if needed.
//
INLINE Stub* Make_Use_Core(
    Stub* binding,  // must be a varlist or a LET patch
    Specifier* next,
    Heart affected
){
    assert(affected == REB_WORD or affected == REB_SET_WORD);
    if (IS_VARLIST(binding)) {
        if (
            REB_MODULE != CTX_TYPE(cast(Context*, binding))
            and CTX_LEN(cast(Context*, binding)) == 0  // nothing to bind to
        ){
            return next;
        }
    }
    else
        assert(IS_LET(binding));

    // It's possible for a user to try and doubly virtual bind things...but
    // for the moment assume it only happens on accident and alert us to it.
    // Over the long run, this needs to be legal, though.
    //
    if (next and IS_USE(next)) {
        assert(BINDING(Stub_Cell(next)) != binding);
    }

    // A virtual bind patch array is a singular node holding an ANY-WORD?
    // bound to the OBJECT! being virtualized against.  The reason for holding
    // the WORD! instead of the OBJECT! in the array cell are:
    //
    // * Gives more header information than storing information already
    //   available in the archetypal context.  So we can assume things like
    //   a SET-WORD! means "only virtual bind the set-words".
    //
    // * Can be used to bind to the last word in the context at the time of
    //   the virtual bind.  This allows for expansion.  The problem with
    //   just using however-many-items-are-current is that it would mean
    //   the extant cached virtual index information could not be trusted.
    //   This gives reproducible effects on when you'll get hits or misses
    //   instead of being subject to the whim of internal cache state.
    //
    // * If something changes the CTX_TYPE() that doesn't have to be reflected
    //   here.  This is a rare case, but happens with MAKE ERROR! in startup
    //   because the standard error object starts life as an object.  (This
    //   mechanism needs revisiting, but it's just another reason.)
    //
    Array* use = Alloc_Singular(
        //
        // INODE is not used yet (likely application: symbol for patches that
        // represent lets).  Consider uses in patches that represent objects.
        // So no SERIES_FLAG_INFO_NODE_NEEDS_MARK yet.
        //
        // MISC is a node, but it's used for linking patches to variants
        // with different chains underneath them...and shouldn't keep that
        // alternate version alive.  So no SERIES_FLAG_MISC_NODE_NEEDS_MARK.
        //
        FLAG_FLAVOR(USE)
            | NODE_FLAG_MANAGED
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK
    );

    if (
        IS_VARLIST(binding)
        and CTX_TYPE(cast(Context*, binding)) == REB_MODULE
    ){
        //
        // Modules have a hash table so they can be searched somewhat quickly
        // for keys.  But keys can be added and removed without a good way
        // of telling the historical order.  Punt on figuring out the answer
        // for it and just let virtual binds see the latest situation.
        //
        Init_Context_Cell(
            Stub_Cell(use),
            REB_MODULE,
            cast(Context*, binding)
        );
    }
    else {
        const Symbol* symbol;  // can't use ?: with INODE
        if (IS_VARLIST(binding)) {
            symbol = KEY_SYMBOL(CTX_KEY(cast(Context*, binding), 1));
            Init_Any_Word_Bound_Untracked(  // arbitrary word
                TRACK(Stub_Cell(use)),
                affected,
                symbol,
                binding,
                1  // arbitrary first word
            );
        }
        else {
            symbol = INODE(LetSymbol, binding);
            Init_Any_Word_Bound_Untracked(  // arbitrary word
                TRACK(Stub_Cell(use)),
                affected,
                symbol,
                binding,
                INDEX_PATCHED  // the only word in the LET
            );
        }
    }

    // The way it is designed, the list of use/lets terminates in either a
    // nullptr or a context pointer that represents the specifying frame for
    // the chain.  So we can simply point to the existing specifier...whether
    // it is a use, a let, a frame context, or nullptr.
    //
    LINK(NextUse, use) = next;

    // A circularly linked list of variations of this use with different
    // NextVirtual() data is maintained, to assist in avoiding creating
    // unnecessary duplicates.  Decay_Series() will remove this patch from the
    // list when it is being GC'd.
    //
    // !!! This feature was removed for the moment, see notes on Variant.
    //
    MISC(Variant, use) = nullptr;

    INODE(UseReserved, use) = nullptr;  // no application yet

    return use;
}
