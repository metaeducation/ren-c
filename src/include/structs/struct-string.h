//
//  file: %struct-string.h
//  summary: "String structure definitions preceding %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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


//=//// SYMBOL_FLAG_ALL_ASCII /////////////////////////////////////////////=//
//
// Symbols don't store a MISC_STRING_NUM_CODEPOINTS (need space in the Stub
// for other properties).  They're assumed to be short, so counting their
// codepoints isn't that slow.  But since they're immutable, we can save
// whether they're all ASCII at creation time, tells us their num_codepoints
// is the same as their byte size, and speeds up seeking to O(1).
//
#define SYMBOL_FLAG_ALL_ASCII \
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


//=//// SYMBOL_FLAG_ILLEGAL_IN_CHAIN //////////////////////////////////////=//
//
// This basically just means the symbol has a colon in it...other prohibitions
// are covered by ILLEGAL_IN_ANY_SEQUENCE.
//
#define SYMBOL_FLAG_ILLEGAL_IN_CHAIN \
    STUB_SUBCLASS_FLAG_26


//=//// SYMBOL_FLAG_ILLEGAL_IN_TUPLE //////////////////////////////////////=//
//
// This basically just means the symbol has a dot in it...other prohibitions
// are covered by ILLEGAL_IN_ANY_SEQUENCE.
//
#define SYMBOL_FLAG_ILLEGAL_IN_TUPLE \
    STUB_SUBCLASS_FLAG_27


//=//// SYMBOL_FLAG_HITCH_IS_BIND_STUMP ///////////////////////////////////=//
//
// This flag caches a test that today could be written as:
//
//     Stub_Flavor(MISC_HITCH(symbol)) == FLAVOR_STUMP
//
// The flag originated prior to the existence of "stub flavors", so the only
// way to know that a stub wasn't a module Patch stub was to test a flag.
//
// Now that there are flavors, the flag is only an optimization, but maybe
// not a terrible one--since it means binding can quickly check a flag that
// lives on the Symbol without having to dereference a pointer to navigate
// to the next stub and then extract and test a flavor byte for it.  If
// flags were scarce or the maintenance cost was high, this could be dropped,
// but it seems to work well enough for now.
//
#define SYMBOL_FLAG_HITCH_IS_BIND_STUMP \
    STUB_SUBCLASS_FLAG_28


// For a *read-only* Symbol, circularly linked list of othEr-CaSed string
// forms.  It should be relatively quick to find the canon form on
// average, since many-cased forms are somewhat rare.
//
// 1. One synonym need not keep another alive, because the process of freeing
//    string nodes unlinks them from the list.  (Hence the canon can change!)
//
#define STUB_MASK_SYMBOL \
    (BASE_FLAG_BASE \
        | FLAG_FLAVOR(FLAVOR_SYMBOL) \
        | FLEX_FLAG_FIXED_SIZE \
        | BASE_FLAG_MANAGED \
        | STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY  /* kill interning in table */ \
        | not STUB_FLAG_MISC_NEEDS_MARK  /* hitches not marked */ \
        | not STUB_FLAG_LINK_NEEDS_MARK  /* synonym not marked [1] */)

#define STUB_MASK_STRING \
    (FLAG_FLAVOR(FLAVOR_NONSYMBOL) \
        | STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY  /* needs to kill bookmarks */)

#define STUB_MASK_SYMBOL_STRING_COMMON \
    (BASE_FLAG_BASE \
        | STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY)


#define MISC_HITCH(symbol_or_patch_or_stump) \
    STUB_MISC_UNMANAGED(symbol_or_patch_or_stump)


//=//// SYMBOL STRING STUB SLOT USAGE /////////////////////////////////////=//

#define LINK_SYMBOL_SYNONYM(symbol)  STUB_LINK_UNMANAGED(symbol)
// MISC for Symbol is MISC_HITCH()
// INFO is the SymId plus some flags
// BONUS is not currently used...


//=//// NON-SYMBOL STRING STUB SLOT USAGE /////////////////////////////////=//

#define LINK_STRING_BOOKMARKS(s)        STUB_LINK_UNMANAGED(s)
#define MISC_STRING_NUM_CODEPOINTS(s)   (s)->misc.length
// INFO is currently used for info flags regarding locking, etc.
// BONUS is used for biasing from head of allocation


//=//// KEY (POINTER TO SYMBOL) ///////////////////////////////////////////=//
//
// We want to be able to enumerate keys by incrementing across them.  The
// things we increment across aren't Symbol Stubs, but pointers to Symbol
// Stubs... so a Key* is a pointer to a pointer.
//
typedef const Symbol* Key;


//=//// BOOKMARKS (codepoint position caches for UTF-8) ///////////////////=//

struct BookmarkStruct {
    REBLEN index;
    Size offset;
};
typedef struct BookmarkStruct Bookmark;


//=//// SYMBOL OR VALUE CONVENIENCE CLASS /////////////////////////////////=//
//
// When you make a call to make errors, you can pass a Symbol* or a Value*.
// In the C++ build we simply accept either and make it possible to extract
// as a void* suitable for passing to variadics, which then can use
// Detect_Rebol_Pointer() to figure out what it is.
//
#if NO_CPLUSPLUS_11
    #define SymbolOrValue(const_star) \
        void const_star

    #define Extract_SoV(sov) \
        (sov)
#else
    struct SymbolOrValueHolder {
        const void* p;

        SymbolOrValueHolder(const Symbol* s) : p (s) {}
        SymbolOrValueHolder(const Value* v) : p (v) {}

      #if NEEDFUL_OPTION_USES_WRAPPER  // Option(const Symbol*) <> const Symbol*
        SymbolOrValueHolder(Option(const Symbol*)& s) : p (maybe s) {}
      #endif

      #if DEBUG_USE_SINKS
        SymbolOrValueHolder(const NeedWrapper<Value>& v) : p (v.p) {}
        SymbolOrValueHolder(const SinkWrapper<Value>& v) : p (v.p) {}
        SymbolOrValueHolder(const InitWrapper<Value>& v) : p (v.p) {}
        #if CHECK_CELL_SUBCLASSES
            SymbolOrValueHolder(const NeedWrapper<Element>& e) : p (e.p) {}
            SymbolOrValueHolder(const SinkWrapper<Element>& e) : p (e.p) {}
            SymbolOrValueHolder(const InitWrapper<Element>& e) : p (e.p) {}
        #endif
      #endif
    };

    #define SymbolOrValue(const_star) \
        SymbolOrValueHolder

    #define Extract_SoV(sov) \
        (sov).p
#endif
