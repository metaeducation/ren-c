//
//  File: %stub-symbol.h
//  Summary: {Definitions for Symbols and Symbol IDs}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// In Ren-C, words hold a Symbol Flex (String Flex subtype).  They may be GC'd
// (unless they are in the %words.r list, in which case their canon forms are
// protected in order to do SYM_XXX switch statements in the C source, etc.)
//
// There is a global hash table which accelerates finding a word's Symbol
// stub from a UTF-8 source string.  Entries are added to it when new canon
// forms of words are created, and removed when they are GC'd.  It is scaled
// according to the total number of canons in the system.
//
// A SymId refers to one of the built-in words and can be used in C switch
// statements.  A canon Symbol is used to identify everything else.
//
// R3-Alpha's concept was that all words got persistent integer values, which
// prevented garbage collection.  Ren-C only gives built-in words integer
// values--or SymIds--while others must be compared by pointers to their
// name or canon-name pointers.  A non-built-in symbol will return SYM_0 as
// its symbol ID, allowing it to fall through to defaults in case statements.
//

// Some places permit an optional label (such as the names of function
// invocations, which may not have an associated name).  To make the callsite
// intent clearer for passing in a null Symbol*, use ANONYMOUS instead.
//
#if DEBUG_CHECK_OPTIONALS
    #define ANONYMOUS   Option(const Symbol*){nullptr}
#else
    #define ANONYMOUS   nullptr
#endif


// For a *read-only* Symbol, circularly linked list of othEr-CaSed string
// forms.  It should be relatively quick to find the canon form on
// average, since many-cased forms are somewhat rare.
//
// Note: A String Flex using this doesn't have FLEX_FLAG_LINK_NODE_NEEDS_MARK.
// One synonym need not keep another alive, because the process of freeing
// string nodes unlinks them from the list.  (Hence the canon can change!)
//
#define LINK_Synonym_TYPE       const Symbol*
#define HAS_LINK_Synonym        FLAVOR_SYMBOL

// Hitches are a circularly linked list that includes transient binding info
// for the word, as well as declared variables in "sea" contexts.
//
#define MISC_Hitch_TYPE         Flex*
#define HAS_MISC_Hitch          FLAVOR_SYMBOL

#define LINK_NextBind_TYPE      Stub*
#define HAS_LINK_NextBind       FLAVOR_HITCH

#define INODE_BindSymbol_TYPE   const Symbol*
#define HAS_INODE_BindSymbol    FLAVOR_HITCH


//=//// SYMBOL_FLAG_24 ////////////////////////////////////////////////////=//
//
#define SYMBOL_FLAG_24 \
    FLEX_FLAG_24


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
    FLEX_FLAG_25


//=//// SYMBOL_FLAG_ILLEGAL_IN_ANY_CHAIN //////////////////////////////////=//
//
// This basically just means the symbol has a colon in it...other prohibitions
// are covered by ILLEGAL_IN_ANY_SEQUENCE.
//
#define SYMBOL_FLAG_ILLEGAL_IN_ANY_CHAIN \
    FLEX_FLAG_26


//=//// SYMBOL_FLAG_ILLEGAL_IN_ANY_TUPLE ///////////////////////////////////=//
//
// This basically just means the symbol has a dot in it...other prohibitions
// are covered by ILLEGAL_IN_ANY_SEQUENCE.
//
#define SYMBOL_FLAG_ILLEGAL_IN_ANY_TUPLE \
    FLEX_FLAG_27


//=//// SYMBOL_FLAG_MISC_IS_BINDINFO ///////////////////////////////////////=//
//
// The symbol hash table itself doubles as a "binding table", by making an
// in-progress bind point to a small stub to hold a binding index into an
// object.  It's fastest for binding to keep this flag on the symbol (an
// original incarnation had to follow the misc pointer and check a flag
// on the next pointer that was reached, which was slower).
//
#define SYMBOL_FLAG_MISC_IS_BINDINFO \
    FLEX_FLAG_28


INLINE bool Is_String_Symbol(const String* s) {
    if (Stub_Flavor(s) == FLAVOR_SYMBOL)
        return true;
    assert(Stub_Flavor(s) == FLAVOR_NONSYMBOL);
    return false;
}

#if CPLUSPLUS_11  // disable superfluous check that Symbol has FLAVOR_SYMBOL
    INLINE bool Is_String_Symbol(const Symbol* s) = delete;
#endif


INLINE Option(SymId) Symbol_Id(const Symbol* s)
  { return cast(SymId, SECOND_UINT16(&s->info)); }

INLINE const Symbol* Canon_Symbol(SymId symid) {
    assert(cast(uint16_t, symid) != 0);
    assert(cast(uint16_t, symid) < ALL_SYMS_MAX);
    return &g_symbols.builtin_canons[symid];
}

#define Canon(name) \
    Canon_Symbol(SYM_##name)

INLINE bool Are_Synonyms(const Symbol* s1, const Symbol* s2) {
    const Symbol* temp = s1;
    do {
        if (temp == s2)
            return true;
    } while ((temp = LINK(Synonym, temp)) != s1);

    return false;  // stopped when circularly linked list loops back to self
}

#define Intern_UTF8_Managed(utf8,size) \
    Intern_UTF8_Managed_Core(nullptr, (utf8), (size))


// When you pass a symbol to the variadic API interfaces, it assumes that you
// want to make a plain WORD! with that symbol.  This is faster than needing
// to allocate a separate word for the purpose of passing in.
//
// This doesn't actually do anything--just passes the symbol through.  But
// it's needed for typechecking in C++, because it doesn't accept arbitrary
// void pointers...only things it knows about.  Symbol isn't one of the things
// exported in the API, so we have to approve its use in API variadics here.
//
#if (! LIBREBOL_NO_CPLUSPLUS)
    inline const void* to_rebarg(const Symbol* symbol)
        { return symbol; }
#endif
