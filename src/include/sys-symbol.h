//
//  File: %sys-symbol.h
//  Summary: {Definitions for Symbols and Symbol IDs}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// In Ren-C, words are Symbol series (String subtype).  They may be GC'd
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
// Though it works fine for switch statements, it creates a problem if someone
// writes `VAL_WORD_ID(a) == VAL_WORD_ID(b)`, because all non-built-ins
// will appear to be equal.  It's a tricky enough bug to catch to warrant an
// extra check in C++ that disallows comparing SYMIDs with ==
//


// For a *read-only* Symbol, circularly linked list of othEr-CaSed string
// forms.  It should be relatively quick to find the canon form on
// average, since many-cased forms are somewhat rare.
//
// Note: String series using this don't have SERIES_FLAG_LINK_NODE_NEEDS_MARK.
// One synonym need not keep another alive, because the process of freeing
// string nodes unlinks them from the list.  (Hence the canon can change!)
//
#define LINK_Synonym_TYPE       Symbol(const*)
#define LINK_Synonym_CAST       SYM
#define HAS_LINK_Synonym        FLAVOR_SYMBOL

// Hitches are a circularly linked list that includes transient binding info
// for the word, as well as declared variables in "sea" contexts.
//
#define MISC_Hitch_TYPE         Series(*)
#define MISC_Hitch_CAST         SER
#define HAS_MISC_Hitch          FLAVOR_SYMBOL


//=//// SYMBOL_FLAG_ESCAPE_PLAIN ///////////////////////////////////////////=//
//
// Some symbols need to be escaped even when not in paths/tuples/set/get/etc:
//
//   * Anything with a space in it, obviously
//   * Anything with a dot or slash in it, that isn't all dots or all slashes
//   * Anything with a comma in it
//
// etc.  Examples of things that don't need to be plain-escaped would be
// stuff like `::` or `@`
//
#define SYMBOL_FLAG_ESCAPE_PLAIN \
    SERIES_FLAG_24


//=//// SYMBOL_FLAG_ESCAPE_WITH_SIGIL //////////////////////////////////////=//
//
// These are things that have to be escaped when used with a simple SET-WORD!
// or META-WORD!, etc.  Examples of things that don't would be `///` or `.`
//
#define SYMBOL_FLAG_ESCAPE_WITH_SIGIL \
    SERIES_FLAG_25


//=//// SYMBOL_FLAG_ESCAPE_IN_SEQUENCE /////////////////////////////////////=//
//
// Some symbols cannot appear in PATHs or TUPLEs, or SET-WORD!s: notably
// anything that has dots, slashes, or spaces.
//
//    object.|employee.name|
//
#define SYMBOL_FLAG_ESCAPE_IN_SEQUENCE \
    SERIES_FLAG_26


inline static Option(SymId) ID_OF_SYMBOL(Symbol(const*) s)
  { return cast(SymId, SECOND_UINT16(&s->info)); }

inline static Symbol(const*) Canon_Symbol(SymId symid) {
    assert(cast(REBLEN, symid) != 0);
    assert(cast(REBLEN, symid) < ALL_SYMS_MAX);
    return &g_symbols.builtin_canons[symid];
}

#define Canon(name) \
    Canon_Symbol(SYM_##name)

inline static bool Are_Synonyms(Symbol(const*) s1, Symbol(const*) s2) {
    Symbol(const*) temp = s1;
    do {
        if (temp == s2)
            return true;
    } while ((temp = LINK(Synonym, temp)) != s1);

    return false;  // stopped when circularly linked list loops back to self
}

#define Intern_UTF8_Managed(utf8,size) \
    Intern_UTF8_Managed_Core(nullptr, (utf8), (size))
