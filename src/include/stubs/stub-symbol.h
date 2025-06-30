//
//  file: %stub-symbol.h
//  summary: "Definitions for Symbols and Symbol IDs"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// In Ren-C, words hold a Symbol Flex (Strand Flex subtype).  They may be GC'd
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
// invocations, which may not have an associated name).  To make sure the
// callsite intends to accept symbols, use ANONYMOUS instead of nullptr.
//
#define ANONYMOUS  u_cast(Option(const Symbol*), nullptr)


INLINE bool Is_Strand_Symbol(const Strand* s) {
    if (Stub_Flavor(s) == FLAVOR_SYMBOL)
        return true;
    assert(Stub_Flavor(s) == FLAVOR_NONSYMBOL);
    return false;
}

#if CPLUSPLUS_11  // disable superfluous check that Symbol has FLAVOR_SYMBOL
    INLINE bool Is_Strand_Symbol(const Symbol* s) = delete;
#endif


INLINE Option(SymId) Symbol_Id(const Symbol* s)
  { return cast(SymId, SECOND_UINT16(&s->info.flags)); }

INLINE const Symbol* Canon_Symbol(SymId symid) {
    assert(symid != SYM_0_constexpr);
    assert(cast(SymId16, symid) <= MAX_SYM_BUILTIN);
    return &g_symbols.builtin_canons[symid];
}

#define CANON(name) \
    Canon_Symbol(SYM_##name)

INLINE const Symbol* Link_Next_Synonym(const Symbol* symbol) {
    const Symbol* synonym = cast(const Symbol*, LINK_SYMBOL_SYNONYM(symbol));
    possibly(synonym == symbol);  // circularly linked list
    return synonym;
}

INLINE void Tweak_Link_Next_Synonym(Stub* symbol, const Stub* synonym) {
    assert(Is_Stub_Symbol(symbol));
    possibly(synonym == symbol);  // circularly linked list
    LINK_SYMBOL_SYNONYM(symbol) = m_cast(Stub*, synonym);  // extract as const
}

INLINE bool Are_Synonyms(const Symbol* s1, const Symbol* s2) {
    const Symbol* temp = s1;
    do {
        if (temp == s2)
            return true;
    } while ((temp = Link_Next_Synonym(temp)) != s1);

    return false;  // stopped when circularly linked list loops back to self
}

#define Intern_Utf8_Managed(utf8,size) \
    Intern_Utf8_Managed_Core(nullptr, (utf8), (size))


// Hitches are a circularly linked list that includes transient binding info
// for the word, as well as declared variables in "sea" contexts.

INLINE Stub* Misc_Hitch(const Stub* stub) {
    Flavor flavor_stub = Stub_Flavor(stub);
    assert(
        flavor_stub == FLAVOR_SYMBOL
        or flavor_stub == FLAVOR_STUMP
        or flavor_stub == FLAVOR_PATCH
    );
    UNUSED(flavor_stub);
    Stub* hitch = u_cast(Stub*, MISC_HITCH(stub));
    Flavor flavor_hitch = Stub_Flavor(hitch);
    assert(
        flavor_hitch == FLAVOR_SYMBOL
        or flavor_hitch == FLAVOR_STUMP
        or flavor_hitch == FLAVOR_PATCH
    );
    UNUSED(flavor_hitch);
    return hitch;
}

INLINE void Tweak_Misc_Hitch(Stub* stub, Stub* hitch) {
    Flavor flavor_stub = Stub_Flavor(stub);
    assert(
        flavor_stub == FLAVOR_SYMBOL
        or flavor_stub == FLAVOR_STUMP
        or flavor_stub == FLAVOR_PATCH
    );
    UNUSED(flavor_stub);
    Flavor flavor_hitch = Stub_Flavor(hitch);
    assert(
        flavor_hitch == FLAVOR_SYMBOL
        or flavor_hitch == FLAVOR_STUMP
        or flavor_hitch == FLAVOR_PATCH
    );
    UNUSED(flavor_hitch);
    MISC_HITCH(stub) = hitch;
}


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
