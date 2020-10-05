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



// Historically, it was popular for routines that wanted BINARY! data to also
// accept a STRING!, which would be automatically converted to UTF-8 binary
// data.  This makes those more convenient to write.
//
// !!! With the existence of AS, this might not be as useful as leaving
// STRING! open for a different meaning (or an error as a sanity check).
//
inline static const REBYTE *VAL_BYTES_LIMIT_AT(
    REBSIZ *size_out,
    const RELVAL *v,
    REBLEN limit
){
    if (limit == UNKNOWN || limit > VAL_LEN_AT(v))
        limit = VAL_LEN_AT(v);

    if (IS_BINARY(v)) {
        *size_out = limit;
        return VAL_BIN_AT(v);
    }

    if (ANY_STRING(v)) {
        *size_out = VAL_SIZE_LIMIT_AT(NULL, v, limit);
        return VAL_STRING_AT(v);
    }

    assert(ANY_WORD(v));
    assert(limit == VAL_LEN_AT(v)); // !!! TBD: string unification

    const REBSTR *spelling = VAL_WORD_SPELLING(v);
    *size_out = STR_SIZE(spelling);
    return STR_HEAD(spelling); 
}

#define VAL_BYTES_AT(size_out,v) \
    VAL_BYTES_LIMIT_AT((size_out), (v), UNKNOWN)


// Analogous to VAL_BYTES_AT, some routines were willing to accept either an
// ANY-WORD! or an ANY-STRING! to get UTF-8 data.  This is a convenience
// routine for handling that.
//
inline static const REBYTE *VAL_UTF8_LIMIT_AT(
    REBLEN *length_out,
    REBSIZ *size_out,
    REBCEL(const*) v,
    REBLEN limit
){
  #if !defined(NDEBUG)
    REBSIZ dummy_size;
    if (not size_out)
        size_out = &dummy_size;  // force size calculation for debug check
  #endif

    REBCHR(const*) utf8;
    if (ANY_STRING_KIND(CELL_KIND(v))) {
        utf8 = VAL_STRING_AT(v);

        if (size_out or length_out) {
            REBSIZ utf8_size = VAL_SIZE_LIMIT_AT(length_out, v, limit);

            // Protect against embedded '\0' in debug build, which are illegal
            // in ANY-STRING!, and mess up clients who go by NUL terminators.
            //
          #if !defined(NDEBUG)
            REBLEN n;
            for (n = 0; n < utf8_size; ++n)
                assert(utf8[n] != '\0');
          #endif

            if (size_out)
                *size_out = utf8_size;
            // length_out handled by VAL_SIZE_LIMIT_AT, even if nullptr
        }
    }
    else {
        assert(ANY_WORD_KIND(CELL_KIND(v)));

        const REBSTR *spelling = VAL_WORD_SPELLING(v);
        utf8 = STR_HEAD(spelling);

        if (size_out or length_out) {
            if (limit == UNKNOWN and not length_out)
                *size_out = STR_SIZE(spelling);
            else {
                // WORD!s don't cache their codepoint length, must calculate
                //
                REBCHR(const*) cp = utf8;
                REBLEN index = 0;
                for (index = 0; index < limit; ++index, cp = NEXT_STR(cp)) {
                    if (CHR_CODE(cp) == '\0')
                        break;
                }
                if (size_out)
                    *size_out = cp - utf8;
                if (length_out)
                    *length_out = index;
            }
        }
    }

    return utf8; 
}

#define VAL_UTF8_AT(size_out,v) \
    VAL_UTF8_LIMIT_AT(nullptr, (size_out), (v), UNKNOWN)


// To make interfaces easier for some functions that take REBSTR* strings,
// it can be useful to allow passing UTF-8 text, a REBVAL* with an ANY-WORD!
// or ANY-STRING!, or just plain UTF-8 text.
//
// !!! Should NULLED_CELL or other arguments make anonymous symbols?
//
#ifdef CPLUSPLUS_11
template<typename T>
inline static const REBSTR* Intern(const T *p)
{
    static_assert(
        std::is_same<T, REBVAL>::value
        or std::is_same<T, char>::value
        or std::is_same<T, REBSTR>::value,
        "STR works on: char*, REBVAL*, REBSTR*"
    );
#else
inline static const REBSTR* Intern(const void *p)
{
#endif
    switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8: {
        const char *utf8 = cast(const char*, p);
        return Intern_UTF8_Managed(cb_cast(utf8), strsize(utf8)); }

      case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p));
        assert(GET_SERIES_FLAG(s, IS_STRING));
        return STR(s); }

      case DETECTED_AS_CELL: {
        const REBVAL *v = cast(const REBVAL*, p);
        if (ANY_WORD(v))
            return VAL_WORD_SPELLING(v);

        assert(ANY_STRING(v));

        // You would not want the change of `file` to affect the filename
        // references in x's loaded source.
        //
        //     file: copy %test
        //     x: transcode/file data1 file
        //     append file "-2"
        //     y: transcode/file data2 file
        //
        // So mutable series shouldn't be used directly.  Reuse the string
        // interning mechanics to cut down on storage.
        //
        // !!! With UTF-8 Everywhere, could locked strings be used here?
        // Should all locked strings become interned, and forward pointers
        // to the old series in the background to the interned version?
        //
        // !!! We know the length in codepoints, should we pass it to the
        // Intern_UTF8 function to store?  Does it usually have to scan to
        // calculate this, or can it be done on demand?
        //
        REBSIZ utf8_size;
        const REBYTE *utf8 = VAL_UTF8_AT(&utf8_size, v);
        return Intern_UTF8_Managed(utf8, utf8_size); }

      default:
        panic ("Bad pointer type passed to Intern()");
    }
}

