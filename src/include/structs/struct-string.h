//
//  File: %struct-string.h
//  Summary: "String structure definitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
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

#if CPLUSPLUS_11
    struct String : public Binary {};  // UTF8-constrained Binary
    struct Symbol : public String {};  // WORD!-constrained immutable String

    struct BookmarkList : public Flex {};
#else
    typedef Flex String;
    typedef Flex Symbol;

    typedef Flex BookmarkList;
#endif


//=//// SYMBOL_FLAG_24 ////////////////////////////////////////////////////=//
//
#define SYMBOL_FLAG_24 \
    STUB_SUBCLASS_FLAG_24


//=//// SYMBOL_FLAG_ILLEGAL_IN_ANY_SEQUENCE ////////////////////////////////=//
//
// Symbols with < or > in them do not work in sequences:
//
//    >> make path! [<| |>]
//    == <|/|>  ; should be a tag
//
// Also, slashes are not allowed in paths or tuples (but dots are ok in paths)
//
#define SYMBOL_FLAG_ILLEGAL_IN_ANY_SEQUENCE \
    STUB_SUBCLASS_FLAG_25


//=//// SYMBOL_FLAG_ILLEGAL_IN_ANY_CHAIN //////////////////////////////////=//
//
// This basically just means the symbol has a colon in it...other prohibitions
// are covered by ILLEGAL_IN_ANY_SEQUENCE.
//
#define SYMBOL_FLAG_ILLEGAL_IN_ANY_CHAIN \
    STUB_SUBCLASS_FLAG_26


//=//// SYMBOL_FLAG_ILLEGAL_IN_ANY_TUPLE ///////////////////////////////////=//
//
// This basically just means the symbol has a dot in it...other prohibitions
// are covered by ILLEGAL_IN_ANY_SEQUENCE.
//
#define SYMBOL_FLAG_ILLEGAL_IN_ANY_TUPLE \
    STUB_SUBCLASS_FLAG_27


//=//// SYMBOL_FLAG_MISC_IS_BINDINFO ///////////////////////////////////////=//
//
// The symbol hash table itself doubles as a "binding table", by making an
// in-progress bind point to a small stub to hold a binding index into an
// object.  It's fastest for binding to keep this flag on the symbol (an
// original incarnation had to follow the misc pointer and check a flag
// on the next pointer that was reached, which was slower).
//
#define SYMBOL_FLAG_MISC_IS_BINDINFO \
    STUB_SUBCLASS_FLAG_28


#define FLEX_MASK_SYMBOL \
    (NODE_FLAG_NODE \
        | FLAG_FLAVOR(SYMBOL) \
        | FLEX_FLAG_FIXED_SIZE \
        | NODE_FLAG_MANAGED)


// For a *read-only* Symbol, circularly linked list of othEr-CaSed string
// forms.  It should be relatively quick to find the canon form on
// average, since many-cased forms are somewhat rare.
//
// Note: A String Flex using this doesn't have STUB_FLAG_LINK_NODE_NEEDS_MARK.
// One synonym need not keep another alive, because the process of freeing
// string nodes unlinks them from the list.  (Hence the canon can change!)
//
#define LINK_Synonym_TYPE       const Symbol*
#define HAS_LINK_Synonym        FLAVOR_SYMBOL

// Hitches are a circularly linked list that includes transient binding info
// for the word, as well as declared variables in "sea" contexts.
//
#define MISC_Hitch_TYPE         Stub*
#define HAS_MISC_Hitch          FLAVOR_SYMBOL

#define LINK_NextBind_TYPE      Stub*
#define HAS_LINK_NextBind       FLAVOR_HITCH

#define INODE_BindSymbol_TYPE   const Symbol*
#define HAS_INODE_BindSymbol    FLAVOR_HITCH


//=//// KEY (POINTER TO SYMBOL) ////////////////////////////////////////=//
//
// We want to be able to enumerate keys by incrementing across them.  The
// things we increment across aren't Symbol Stubs, but pointers to Symbol
// Stubs... so a Key* is a pointer to a pointer.
//
typedef const Symbol* Key;
