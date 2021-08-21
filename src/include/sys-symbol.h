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
// A SYM refers to one of the built-in words and can be used in C switch
// statements.  A canon STR is used to identify everything else.
//
// R3-Alpha's concept was that all words got persistent integer values, which
// prevented garbage collection.  Ren-C only gives built-in words integer
// values--or SYMIDs--while others must be compared by pointers to their
// name or canon-name pointers.  A non-built-in symbol will return SYM_0 as
// its symbol ID, allowing it to fall through to defaults in case statements.
//
// Though it works fine for switch statements, it creates a problem if someone
// writes `VAL_WORD_ID(a) == VAL_WORD_ID(b)`, because all non-built-ins
// will appear to be equal.  It's a tricky enough bug to catch to warrant an
// extra check in C++ that disallows comparing SYMIDs with ==
//


// For a *read-only* REBSYM, circularly linked list of othEr-CaSed string
// forms.  It should be relatively quick to find the canon form on
// average, since many-cased forms are somewhat rare.
//
// Note: String series using this don't have SERIES_FLAG_LINK_NODE_NEEDS_MARK.
// One synonym need not keep another alive, because the process of freeing
// string nodes unlinks them from the list.  (Hence the canon can change!)
//
#define LINK_Synonym_TYPE       const REBSYM*
#define LINK_Synonym_CAST       SYM
#define HAS_LINK_Synonym        FLAVOR_SYMBOL

// Hitches are a circularly linked list that includes transient binding info
// for the word, as well as declared variables in "sea" contexts.
//
#define MISC_Hitch_TYPE         REBSER*
#define MISC_Hitch_CAST         SER
#define HAS_MISC_Hitch          FLAVOR_SYMBOL


#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    //
    // Trivial definition for C build or release builds: symbols are just a C
    // enum value and an OPT_SYMID acts just like a SYMID.
    //
    typedef enum Reb_Symbol_Id SYMID;
    typedef enum Reb_Symbol_Id OPT_SYMID;
#else
    struct SYMID;

    struct OPT_SYMID {  // may only be converted to SYMID, no comparisons
        enum Reb_Symbol_Id n;
        OPT_SYMID (const SYMID& sym);
        bool operator==(enum Reb_Symbol_Id other) const
          { return n == other; }
        bool operator!=(enum Reb_Symbol_Id other) const
          { return n != other; }

        bool operator==(OPT_SYMID &&other) const = delete;
        bool operator!=(OPT_SYMID &&other) const = delete;

        operator unsigned int() const  // so it works in switch() statements
          { return cast(unsigned int, n); }

        explicit operator enum Reb_Symbol_Id()  // must be an *explicit* cast
          { return n; }
    };

    struct SYMID {  // acts like a REBOL_Symbol with no OPT_SYMID compares
        enum Reb_Symbol_Id n;
        SYMID () {}
        SYMID (int n) : n (cast(enum Reb_Symbol_Id, n)) {}
        SYMID (OPT_SYMID opt_sym) : n (opt_sym.n) {}

        operator unsigned int() const  // so it works in switch() statements
          { return cast(unsigned int, n); }

        explicit operator enum Reb_Symbol_Id() {  // must be an *explicit* cast
            assert(n != SYM_0);
            return n;
        }

        bool operator>=(enum Reb_Symbol_Id other) const {
            assert(other != SYM_0);
            return n >= other;
        }
        bool operator<=(enum Reb_Symbol_Id other) const {
            assert(other != SYM_0);
            return n <= other;
        }
        bool operator>(enum Reb_Symbol_Id other) const {
            assert(other != SYM_0);
            return n > other;
        }
        bool operator<(enum Reb_Symbol_Id other) const {
            assert(other != SYM_0);
            return n < other;
        }
        bool operator==(enum Reb_Symbol_Id other) const
          { return n == other; }
        bool operator!=(enum Reb_Symbol_Id other) const
          { return n != other; }

        bool operator==(SYMID &other) const = delete;  // may be SYM_0
        void operator!=(SYMID &other) const = delete;  // ...same
        bool operator==(const OPT_SYMID &other) const = delete;  // ...same
        void operator!=(const OPT_SYMID &other) const = delete;  // ...same
    };

    inline OPT_SYMID::OPT_SYMID(const SYMID &sym) : n (sym.n) {}
#endif

inline static bool Same_Nonzero_Symid(SYMID a, SYMID b) {
    assert(a != SYM_0 and b != SYM_0);
    return cast(REBLEN, a) == cast(REBLEN, b);
}

inline static OPT_SYMID ID_OF_SYMBOL(const REBSYM *s)
  { return cast(SYMID, SECOND_UINT16(s->info)); }

inline static const REBSYM *Canon(SYMID symid) {
    assert(cast(REBLEN, symid) != 0);
    assert(cast(REBLEN, symid) < ALL_SYMS_MAX);
    return PG_Symbol_Canons[symid];
}

inline static bool Are_Synonyms(const REBSYM *s1, const REBSYM *s2) {
    const REBSTR *temp = s1;
    do {
        if (temp == s2)
            return true;
    } while ((temp = LINK(Synonym, temp)) != s1);

    return false;  // stopped when circularly linked list loops back to self
}
