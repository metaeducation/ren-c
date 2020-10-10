//
//  File: %sys-word.h
//  Summary: {Definitions for the ANY-WORD! Datatypes}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// The ANY-WORD! is the fundamental symbolic concept of Rebol.  It is
// implemented as a REBSTR UTF-8 string (see %sys-string.h), but rather than
// hold "bookmark" caches of indexing positions into its data (which is
// generally quite short and not iterated), it stores links to "synonyms"
// of alternate spellings which share the same symbol ID.
//
// ANY-WORD! can act as a variable when bound specifically to a context
// (see %sys-context.h) or bound relatively to an action (see %sys-action.h).
//
// For routines that manage binding, see %sys-bind.h.
//



// REBCTX types use this field of their varlist (which is the identity of
// an ANY-CONTEXT!) to find their "keylist".  It is stored in the REBSER
// node of the varlist REBARR vs. in the REBVAL of the ANY-CONTEXT! so
// that the keylist can be changed without needing to update all the
// REBVALs for that object.
//
// It may be a simple REBARR* -or- in the case of the varlist of a running
// FRAME! on the stack, it points to a REBFRM*.  If it's a FRAME! that
// is not running on the stack, it will be the function paramlist of the
// actual phase that function is for.  Since REBFRM* all start with a
// REBVAL cell, this means NODE_FLAG_CELL can be used on the node to
// discern the case where it can be cast to a REBFRM* vs. REBARR*.
//
// (Note: FRAME!s used to use a field `misc.f` to track the associated
// frame...but that prevented the ability to SET-META on a frame.  While
// that feature may not be essential, it seems awkward to not allow it
// since it's allowed for other ANY-CONTEXT!s.  Also, it turns out that
// heap-based FRAME! values--such as those that come from MAKE FRAME!--
// have to get their keylist via the specifically applicable ->phase field
// anyway, and it's a faster test to check this for NODE_FLAG_CELL than to
// separately extract the CTX_TYPE() and treat frames differently.)
//
// It is done as a base-class REBNOD* as opposed to a union in order to
// not run afoul of C's rules, by which you cannot assign one member of
// a union and then read from another.
//
#define LINK_KEYSOURCE(s)       LINK(s).custom.node

#define INIT_LINK_KEYSOURCE(a,keysource) \
    LINK_KEYSOURCE(a) = (keysource)  // helpful macro for injecting debugging


// For a *read-only* REBSTR, circularly linked list of othEr-CaSed string
// forms.  It should be relatively quick to find the canon form on
// average, since many-cased forms are somewhat rare.
//
// Note: String series using this don't have SERIES_FLAG_LINK_NODE_NEEDS_MARK.
// One synonym need not keep another alive, because the process of freeing
// string nodes unlinks them from the list.  (Hence the canon can change!)
//
#define LINK_SYNONYM_NODE(s)    LINK(s).custom.node
#define LINK_SYNONYM(s)         STR(LINK_SYNONYM_NODE(s))


//=//// SAFE COMPARISONS WITH BUILT-IN SYMBOLS ////////////////////////////=//
//
// A SYM refers to one of the built-in words and can be used in C switch
// statements.  A canon STR is used to identify everything else.
// 
// R3-Alpha's concept was that all words got persistent integer values, which
// prevented garbage collection.  Ren-C only gives built-in words integer
// values--or SYMs--while others must be compared by pointers to their
// name or canon-name pointers.  A non-built-in symbol will return SYM_0 as
// its symbol, allowing it to fall through to defaults in case statements.
//
// Though it works fine for switch statements, it creates a problem if someone
// writes `VAL_WORD_SYM(a) == VAL_WORD_SYM(b)`, because all non-built-ins
// will appear to be equal.  It's a tricky enough bug to catch to warrant an
// extra check in C++ that disallows comparing SYMs with ==

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    //
    // Trivial definition for C build or release builds: symbols are just a C
    // enum value and an OPT_REBSYM acts just like a REBSYM.
    //
    typedef enum Reb_Symbol REBSYM;
    typedef enum Reb_Symbol OPT_REBSYM;
#else
    struct REBSYM;

    struct OPT_REBSYM {  // may only be converted to REBSYM, no comparisons
        enum Reb_Symbol n;
        OPT_REBSYM (const REBSYM& sym);
        bool operator==(enum Reb_Symbol other) const
          { return n == other; }
        bool operator!=(enum Reb_Symbol other) const
          { return n != other; }

        bool operator==(OPT_REBSYM &&other) const = delete;
        bool operator!=(OPT_REBSYM &&other) const = delete;

        operator unsigned int() const  // so it works in switch() statements
          { return cast(unsigned int, n); }

        explicit operator enum Reb_Symbol()  // must be an *explicit* cast
          { return n; }
    };

    struct REBSYM {  // acts like a REBOL_Symbol with no OPT_REBSYM compares
        enum Reb_Symbol n;
        REBSYM () {}
        REBSYM (int n) : n (cast(enum Reb_Symbol, n)) {}
        REBSYM (OPT_REBSYM opt_sym) : n (opt_sym.n) {}

        operator unsigned int() const  // so it works in switch() statements
          { return cast(unsigned int, n); }

        explicit operator enum Reb_Symbol() {  // must be an *explicit* cast
            assert(n != SYM_0);
            return n;
        }

        bool operator>=(enum Reb_Symbol other) const {
            assert(other != SYM_0);
            return n >= other;
        }
        bool operator<=(enum Reb_Symbol other) const {
            assert(other != SYM_0);
            return n <= other;
        }
        bool operator>(enum Reb_Symbol other) const {
            assert(other != SYM_0);
            return n > other;
        }
        bool operator<(enum Reb_Symbol other) const {
            assert(other != SYM_0);
            return n < other;
        }
        bool operator==(enum Reb_Symbol other) const
          { return n == other; }
        bool operator!=(enum Reb_Symbol other) const
          { return n != other; }

        bool operator==(REBSYM &other) const = delete;  // may be SYM_0
        void operator!=(REBSYM &other) const = delete;  // ...same
        bool operator==(const OPT_REBSYM &other) const = delete;  // ...same
        void operator!=(const OPT_REBSYM &other) const = delete;  // ...same
    };

    inline OPT_REBSYM::OPT_REBSYM(const REBSYM &sym) : n (sym.n) {}
#endif

inline static bool SAME_SYM_NONZERO(REBSYM a, REBSYM b) {
    assert(a != SYM_0 and b != SYM_0);
    return cast(REBLEN, a) == cast(REBLEN, b);
}

inline static const REBSTR *STR_CANON(const REBSTR *s) {
    assert(IS_STR_SYMBOL(STR(s)));

    while (NOT_SERIES_INFO(s, STRING_CANON))
        s = LINK_SYNONYM(s);  // circularly linked list
    return s;
}

inline static OPT_REBSYM STR_SYMBOL(const REBSTR *s) {
    assert(IS_STR_SYMBOL(STR(s)));

    uint16_t sym = SECOND_UINT16(SER(s)->header);
    assert(sym == SECOND_UINT16(SER(STR_CANON(s))->header));
    return cast(REBSYM, sym);
}

inline static const REBSTR *Canon(REBSYM sym) {
    assert(cast(REBLEN, sym) != 0);
    assert(cast(REBLEN, sym) < SER_USED(PG_Symbol_Canons));  // null if boot!
    return *SER_AT(const REBSTR*, PG_Symbol_Canons, cast(REBLEN, sym));
}

inline static bool SAME_STR(const REBSTR *s1, const REBSTR *s2) {
    assert(IS_STR_SYMBOL(STR(s1)));
    assert(IS_STR_SYMBOL(STR(s2)));

    if (s1 == s2)
        return true; // !!! does this check speed things up or not?
    return STR_CANON(s1) == STR_CANON(s2); // canon check, quite fast
}


inline static bool IS_WORD_UNBOUND(REBCEL(const*) v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    return not EXTRA(Binding, v).node;
}

#define IS_WORD_BOUND(v) \
    (not IS_WORD_UNBOUND(v))

inline static const REBSTR *VAL_WORD_SPELLING(REBCEL(const*) v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    return STR(PAYLOAD(Any, v).first.node);
}

inline static const REBSTR *VAL_WORD_CANON(REBCEL(const*) v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    return STR_CANON(STR(PAYLOAD(Any, v).first.node));
}

// Some scenarios deliberately store canon spellings in words, to avoid
// needing to re-canonize them.  If you have one of those words, use this to
// add a check that your assumption about them is correct.
//
// Note that canon spellings can get GC'd, effectively changing the canon.
// But they won't if there are any words outstanding that hold that spelling,
// so this is a safe technique as long as these words are GC-mark-visible.
//
inline static REBSTR *VAL_STORED_CANON(REBCEL(const*) v) {
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    REBSTR *str = STR(PAYLOAD(Any, v).first.node);
    assert(GET_SERIES_INFO(str, STRING_CANON));
    return str;
}

inline static OPT_REBSYM VAL_WORD_SYM(REBCEL(const*) v) {
    assert(PG_Symbol_Canons);  // all syms are 0 prior to Init_Symbols()
    assert(ANY_WORD_KIND(CELL_KIND(v)));
    return STR_SYMBOL(STR(PAYLOAD(Any, v).first.node));
}

#define INIT_WORD_INDEX_UNCHECKED(v,i) \
    PAYLOAD(Any, (v)).second.i32 = cast(REBINT, i)

inline static void INIT_WORD_INDEX(RELVAL *v, REBLEN i) {
  #if !defined(NDEBUG)
    INIT_WORD_INDEX_Extra_Checks_Debug(v, i); // not inline, needs FRM_PHASE()
  #endif
    INIT_WORD_INDEX_UNCHECKED(v, i);
}

inline static REBLEN VAL_WORD_INDEX(REBCEL(const*) v) {
    assert(IS_WORD_BOUND(v));
    REBINT i = PAYLOAD(Any, v).second.i32;
    assert(i > 0);
    return cast(REBLEN, i);
}

inline static void Unbind_Any_Word(RELVAL *v) {
    INIT_BINDING(v, UNBOUND);
  #if !defined(NDEBUG)
    INIT_WORD_INDEX_UNCHECKED(v, -1);
  #endif
}

inline static REBVAL *Init_Any_Word(
    RELVAL *out,
    enum Reb_Kind kind,
    const REBSTR *spelling
){
    RESET_CELL(out, kind, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, spelling);
    INIT_BINDING(out, UNBOUND);
  #if !defined(NDEBUG)
    INIT_WORD_INDEX_UNCHECKED(out, -1);  // index not heeded if no binding
  #endif
    return SPECIFIC(out);
}

#define Init_Word(out,str)          Init_Any_Word((out), REB_WORD, (str))
#define Init_Get_Word(out,str)      Init_Any_Word((out), REB_GET_WORD, (str))
#define Init_Set_Word(out,str)      Init_Any_Word((out), REB_SET_WORD, (str))
#define Init_Sym_Word(out,str)      Init_Any_Word((out), REB_SYM_WORD, (str))

inline static REBVAL *Init_Any_Word_Bound(
    RELVAL *out,
    enum Reb_Kind type,
    const REBSTR *spelling,
    REBCTX *context,
    REBLEN index
){
    RESET_CELL(out, type, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, spelling);
    INIT_BINDING(out, context);
    INIT_WORD_INDEX(out, index);
    return SPECIFIC(out);
}


// Helper calls strsize() so you can more easily use literals at callsite.
// (Better to call Intern_UTF8_Managed() with the size if you know it.)
//
inline static const REBSTR *Intern_Unsized_Managed(const char *utf8)
  { return Intern_UTF8_Managed(cb_cast(utf8), strsize(utf8)); }
