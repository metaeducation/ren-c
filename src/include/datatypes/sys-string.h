//
//  File: %sys-string.h
//  Summary: {Definitions for REBSTR (e.g. WORD!) and REBUNI (e.g. STRING!)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The ANY-STRING! and ANY-WORD! data types follows "UTF-8 everywhere", and
// stores all words and strings as UTF-8.  Then it only converts to other
// encodings at I/O points if the platform requires it (e.g. Windows):
//
// http://utf8everywhere.org/
//
// UTF-8 cannot in the general case provide O(1) access for indexing.  We
// attack the problem three ways:
//
// * Avoiding loops which try to access by index, and instead make it easier
//   to smoothly traverse known good UTF-8 data using REBCHR(*).
//
// * Monitoring strings if they are ASCII only and using that to make an
//   optimized jump.  !!! Work in progress, see notes below.
//
// * Maintaining caches (called "Bookmarks") that map from codepoint indexes
//   to byte offsets for larger strings.  These caches must be updated
//   whenever the string is modified.   !!! Only one bookmark per string ATM
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * UTF-8 strings are "byte-sized series", which is also true of BINARY!
//   datatypes.  However, the series used to store UTF-8 strings also store
//   information about their length in codepoints in their series nodes (the
//   main "number of bytes used" in the series conveys bytes, not codepoints).
//   See the distinction between SER_USED() and SER_LEN()
//


//=////////////////////////////////////////////////////////////////////////=//
//
// REBCHR(*) + REBCHR(const*): "ITERATOR" TYPE FOR KNOWN GOOD UTF-8 DATA
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol exchanges UTF-8 data with the outside world via "char*".  But inside
// the code, REBYTE* is used for not-yet-validated bytes that are to be
// scanned as UTF-8.  When accessing an already-checked string, however,
// the REBCHR(*) type is used...signaling no error checking should need to be
// done while walking through the UTF-8 sequence.
//
// So for instance: instead of simply saying:
//
//     REBUNI *ptr = STR_HEAD(string_series);
//     REBUNI c = *ptr++;
//
// ...one must instead write:
//
//     REBCHR(*) ptr = STR_HEAD(string_series);
//     REBUNI c;
//     ptr = NEXT_CHR(&c, ptr);  // ++ptr or ptr[n] will error in C++ build
//
// The code that runs behind the scenes is typical UTF-8 forward and backward
// scanning code, minus any need for error handling.
//
// !!! Error handling is still included due to running common routines, but
// should be factored out for efficiency.
//
#if !defined(CPLUSPLUS_11) or !defined(DEBUG_UTF8_EVERYWHERE)
    //
    // Plain C build uses trivial expansion of REBCHR() and REBCHR(const*)
    //
    //          REBCHR(*) cp; => REBYTE * cp;
    //     REBCHR(const*) cp; => REBYTE const* cp;  // same as `const REBYTE*`
    //
    #define REBCHR(star_or_const_star) \
        REBYTE star_or_const_star

    inline static REBYTE* NEXT_CHR(
        REBUNI *codepoint_out,
        const REBYTE *bp
    ){
        if (*bp < 0x80)
            *codepoint_out = *bp;
        else
            bp = Back_Scan_UTF8_Char(codepoint_out, bp, NULL);
        return m_cast(REBYTE*, bp + 1);
    }

    inline static REBYTE* BACK_CHR(
        REBUNI *codepoint_out,
        const REBYTE *bp
    ){
        NEXT_CHR(codepoint_out, bp);
        --bp;
        while ((*bp & 0xC0) == 0x80)
            --bp;
        return m_cast(REBYTE*, bp);
    }

    inline static REBYTE* NEXT_STR(const REBYTE *bp) {
        do {
            ++bp;
        } while ((*bp & 0xC0) == 0x80);
        return m_cast(REBYTE*, bp);
    }

    inline static REBYTE* BACK_STR(const REBYTE *bp) {
        do {
            --bp;
        } while ((*bp & 0xC0) == 0x80);
        return m_cast(REBYTE*, bp);
    }

    inline static REBYTE* SKIP_CHR(
        REBUNI *codepoint_out,
        const REBYTE *bp,
        REBINT delta
    ){
        REBINT n = delta;
        while (n != 0) {
            if (delta > 0) {
                bp = NEXT_CHR(codepoint_out, bp);
                --n;
            }
            else {
                bp = BACK_CHR(codepoint_out, bp);
                ++n;
            }
        }
        return m_cast(REBYTE*, bp);
    }


    inline static REBUNI CHR_CODE(const REBYTE *bp) {
        REBUNI codepoint;
        NEXT_CHR(&codepoint, bp);
        return codepoint;
    }

    inline static REBYTE* WRITE_CHR(REBYTE* bp, REBUNI codepoint) {
        return bp + Encode_UTF8_Char(bp, codepoint);
    }
#else
    // C++ build uses templates to expand REBCHR(*) and REBCHR(const*) into
    // pointer classes.  This technique allows the simple C compilation too:
    //
    // http://blog.hostilefork.com/kinda-smart-pointers-in-c/
    //
    // NOTE: Don't put this in %reb-defs.h and try to pass REBCHR(*) as args
    // to routines as part of the %sys-core.h API.  This would lead to an
    // incompatible runtime interface between C and C++ builds of cores and
    // extensions using the internal API--which we want to avoid!
    //
    // NOTE: THE NON-INLINED OVERHEAD IS EXTREME IN UNOPTIMIZED BUILDS!  A
    // debug build does not inline these classes and functions.  So traversing
    // strings involves a lot of constructing objects and calling methods that
    // call methods.  Hence these classes are used only in non-debug (and
    // hopefully) optimized builds, where the inlining makes it equivalent to
    // the C version.  That allows for the compile-time type checking but no
    // added runtime overhead.
    //
    template<typename T> struct RebchrPtr;
    #define REBCHR(star_or_const_star) \
        RebchrPtr<REBYTE star_or_const_star>

    // Primary purpose of the classes is to disable the ability to directly
    // increment or decrement pointers to REBYTE* without going through helper
    // routines that do decoding.

    template<>
    struct RebchrPtr<const REBYTE*> {
        const REBYTE *bp;  // will actually be mutable if constructed mutable

        RebchrPtr () {}
        explicit RebchrPtr (const REBYTE *bp) : bp (bp) {}
        explicit RebchrPtr (const char *cstr)
            : bp (cast(const REBYTE*, cstr)) {}

        RebchrPtr next(REBUNI *out) {
            const REBYTE *t = bp;
            if (*t < 0x80)
                *out = *t;
            else
                t = Back_Scan_UTF8_Char(out, t, NULL);
            return RebchrPtr {t + 1};
        }

        RebchrPtr back(REBUNI *out) {
            next(out);
            const REBYTE *t = bp;
            --t;
            while ((*t & 0xC0) == 0x80)
                --t;
            return RebchrPtr {t};
        }

        RebchrPtr next_only() {
            const REBYTE *t = bp;
            do {
                ++t;
            } while ((*t & 0xC0) == 0x80);
            return RebchrPtr {t};
        }

        RebchrPtr back_only() {
            const REBYTE *t = bp;
            do {
                --t;
            } while ((*t & 0xC0) == 0x80);
            return RebchrPtr {t};
        }

        RebchrPtr skip(REBUNI *out, REBINT delta) {
            assert(delta != 0);
            REBINT n = delta;
            const REBYTE *t = bp;  // shouldn't be used
            while (n != 0) {
                if (delta > 0) {
                    t = next(out).bp;
                    --n;
                }
                else {
                    t = back(out).bp;
                    ++n;
                }
            }
            return RebchrPtr {t};
        }

        REBUNI code() {
            REBUNI codepoint;
            next(&codepoint);
            return codepoint;
        }

        REBSIZ operator-(const REBYTE *rhs)
          { return bp - rhs; }

        REBSIZ operator-(RebchrPtr rhs)
          { return bp - rhs.bp; }

        bool operator==(const RebchrPtr<const REBYTE*> &other)
          { return bp == other.bp; }

        bool operator==(const REBYTE *other)
          { return bp == other; }

        bool operator!=(const RebchrPtr<const REBYTE*> &other)
          { return bp != other.bp; }

        bool operator!=(const REBYTE *other)
          { return bp != other; }

        operator const void*() { return bp; }  // implicit cast
        operator const REBYTE*() { return bp; }  // implicit cast
    };

    template<>
    struct RebchrPtr<REBYTE*> : public RebchrPtr<const REBYTE*> {
        RebchrPtr () : RebchrPtr<const REBYTE*>() {}
        explicit RebchrPtr (REBYTE *bp)
            : RebchrPtr<const REBYTE*> (bp) {}
        explicit RebchrPtr (char *cstr)
            : RebchrPtr<const REBYTE*> (cast(REBYTE*, cstr)) {}

        static REBCHR(*) nonconst(REBCHR(const*) cp)
          { return RebchrPtr {m_cast(REBYTE*, cp.bp)}; }

        RebchrPtr back(REBUNI *out)
          { return nonconst(REBCHR(const*)::back(out)); }

        RebchrPtr next(REBUNI *out)
          { return nonconst(REBCHR(const*)::next(out)); }

        RebchrPtr back_only()
          { return nonconst(REBCHR(const*)::back_only()); }

        RebchrPtr next_only()
          { return nonconst(REBCHR(const*)::next_only()); }

        RebchrPtr skip(REBUNI *out, REBINT delta)
          { return nonconst(REBCHR(const*)::skip(out, delta)); }

        RebchrPtr write(REBUNI codepoint) {
            return RebchrPtr {
                m_cast(REBYTE*, bp)
                    + Encode_UTF8_Char(m_cast(REBYTE*, bp), codepoint)
            };
        }

        operator void*() { return m_cast(REBYTE*, bp); }  // implicit cast
        operator REBYTE*() { return m_cast(REBYTE*, bp); }  // implicit cast
    };

    #define NEXT_CHR(out, cp)               (cp).next(out)
    #define BACK_CHR(out, cp)               (cp).back(out)
    #define NEXT_STR(cp)                    (cp).next_only()
    #define BACK_STR(cp)                    (cp).back_only()
    #define SKIP_CHR(out,cp,delta)          (cp).skip((out), (delta))
    #define CHR_CODE(cp)                    (cp).code()
    #define WRITE_CHR(cp, codepoint)        (cp).write(codepoint)
#endif


//=//// REBSTR SERIES FOR UTF8 STRINGS ////////////////////////////////////=//

struct Reb_String {
    struct Reb_Series series;  // http://stackoverflow.com/a/9747062
};

#define STR(p) \
    cast(REBSTR*, (p))  // !!! Enhance with more checks, like SER() does.

inline static bool IS_SER_STRING(REBSER *s) {
    if (NOT_SERIES_FLAG((s), IS_STRING))
        return false;
    assert(SER_WIDE(s) == 1);
    return true;
}

// While the content format is UTF-8 for both ANY-STRING! and ANY-WORD!, the
// MISC() and LINK() fields are used differently.  A string caches its length
// in codepoints so that doesn't have to be recalculated, and it also has
// caches of "bookmarks" mapping codepoint indexes to byte offsets.  Words
// store a pointer that is used in a circularly linked list to find their
// canon spelling form...as well as hold binding information.
//
#define IS_STR_SYMBOL(s) \
    NOT_SERIES_FLAG((s), UTF8_NONWORD)


//=//// STRING ALL-ASCII FLAG /////////////////////////////////////////////=//
//
// One of the best optimizations that can be done on strings is to keep track
// of if they contain only ASCII codepoints.  Such a flag would likely have
// false negatives, unless all removals checked the removed portion for if
// the ASCII flag is true.  It could be then refreshed by any routine that
// walks an entire string for some other reason (like molding or printing).
//
// For the moment, we punt on this optimization.  The main reason is that it
// means the non-ASCII code is exercised on every code path, which is a good
// substitute for finding high-codepoint data to pass through to places that
// would not receive it otherwise.
//
// But ultimately this optimization will be necessary, and decisions on how
// up-to-date the flag should be kept would need to be made.

#define Is_Definitely_Ascii(s) false

inline static bool Is_String_Definitely_ASCII(const RELVAL *str) {
    UNUSED(str);
    return false;
}

inline static const char *STR_UTF8(REBSTR *s) {
    return cast(const char*, BIN_HEAD(SER(s)));
}


inline static size_t STR_SIZE(REBSTR *s) {
    return SER_USED(SER(s)); // number of bytes in series is the UTF-8 size
}

#define STR_HEAD(s) \
    cast(REBCHR(*), SER_HEAD(REBYTE, SER(s)))

#define STR_TAIL(s) \
    cast(REBCHR(*), SER_TAIL(REBYTE, SER(s)))

inline static REBCHR(*) STR_LAST(REBSTR *s) {
    REBCHR(*) cp = STR_TAIL(s);
    REBUNI c;
    cp = BACK_CHR(&c, cp);
    assert(c == '\0');
    UNUSED(c);
    return cp;
}

inline static REBCNT STR_LEN(REBSTR *s) {
    if (Is_Definitely_Ascii(s))
        return STR_SIZE(s);

    if (not IS_STR_SYMBOL(s)) {  // length is cached for non-ANY-WORD! strings
      #if defined(DEBUG_UTF8_EVERYWHERE)
        if (MISC(s).length > SER_USED(s)) // includes 0xDECAFBAD
            panic(s);
      #endif
        return MISC(s).length;
    }

    // Have to do it the slow way if it's a symbol series...but hopefully
    // they're not too long (since spaces and newlines are illegal.)
    //
    REBCNT len = 0;
    REBCHR(const*) ep = STR_TAIL(s);
    REBCHR(const*) cp = STR_HEAD(s);
    while (cp != ep) {
        cp = NEXT_STR(cp);
        ++len;
    }
    return len;
}

inline static REBCNT STR_INDEX_AT(REBSTR *s, REBSIZ offset) {
    if (Is_Definitely_Ascii(s))
        return offset;

    assert(*BIN_AT(SER(s), offset) < 0x80);  // must be codepoint boundary

    if (not IS_STR_SYMBOL(s)) {  // length is cached for non-ANY-WORD! strings
      #if defined(DEBUG_UTF8_EVERYWHERE)
        if (MISC(s).length > SER_USED(s)) // includes 0xDECAFBAD
            panic(s);
      #endif

        // We have length and bookmarks.  We should build STR_AT() based on
        // this routine.  For now, fall through and do it slowly.
    }

    // Have to do it the slow way if it's a symbol series...but hopefully
    // they're not too long (since spaces and newlines are illegal.)
    //
    REBCNT index = 0;
    REBCHR(const*) ep = cast(REBCHR(const*), BIN_AT(SER(s), offset));
    REBCHR(const*) cp = STR_HEAD(s);
    while (cp != ep) {
        cp = NEXT_STR(cp);
        ++index;
    }
    return index;
}

// If you already know what kind of series you have, you should call STR_LEN()
// or SER_USED() (aliased as BIN_LEN(), ARR_LEN(), etc.)  It's rare that you
// don't actually know which it should be.
//
inline static REBCNT SER_LEN(REBSER *s) {  // Generic REBSER length
    if (NOT_SERIES_FLAG(s, IS_STRING))
        return SER_USED(s);
    return STR_LEN(STR(s));
}

inline static void SET_STR_LEN_SIZE(REBSTR *s, REBCNT len, REBSIZ used) {
    assert(not IS_STR_SYMBOL(s));

    SET_SERIES_USED(SER(s), used);
    MISC(s).length = len;
}

inline static void TERM_STR_LEN_SIZE(REBSTR *s, REBCNT len, REBSIZ used) {
    SET_STR_LEN_SIZE(s, len, used);
    TERM_SEQUENCE(SER(s));
}



//=//// CACHED ACCESSORS AND BOOKMARKS ////////////////////////////////////=//
//
// A "bookmark" in this terminology is simply a small REBSER-sized node which
// holds a mapping from an index to an offset in a string.  It is pointed to
// by the string's LINK() field in the series node.

#define BMK_INDEX(b) \
    PAYLOAD(Bookmark, ARR_SINGLE(b)).index

#define BMK_OFFSET(b) \
    PAYLOAD(Bookmark, ARR_SINGLE(b)).offset

inline static REBBMK* Alloc_Bookmark(void) {
    REBARR *bookmark = Alloc_Singular(SERIES_FLAG_MANAGED);
    CLEAR_SERIES_FLAG(bookmark, MANAGED);  // so it's manual but untracked
    LINK(bookmark).bookmarks = nullptr;
    RESET_CELL(ARR_SINGLE(bookmark), REB_X_BOOKMARK, CELL_MASK_NONE);

    // For the moment, REB_X_BOOKMARK is a high numbered type, which keeps
    // it out of the type list *but* means it claims bindability.  Setting
    // its mirror byte to claim it is REB_LOGIC preserves some debuggability
    // (its main type is still bookmark) but makes Is_Bindable() false
    //
    mutable_MIRROR_BYTE(ARR_SINGLE(bookmark)) = REB_LOGIC;
    return bookmark;
}

inline static void Free_Bookmarks_Maybe_Null(REBSTR *s) {
    assert(not IS_STR_SYMBOL(s));  // call on string
    if (LINK(s).bookmarks)
        GC_Kill_Series(SER(LINK(s).bookmarks));  // recursive free whole list
    LINK(s).bookmarks = nullptr;
}

#if !defined(NDEBUG)
    inline static void Check_Bookmarks_Debug(REBSTR *s) {
        REBBMK *bookmark = LINK(s).bookmarks;
        if (not bookmark)
            return;

        assert(not LINK(SER(bookmark)).bookmarks);

        REBCNT index = BMK_INDEX(bookmark);
        REBSIZ offset = BMK_OFFSET(bookmark);

        REBCHR(*) cp = STR_HEAD(s);
        REBCNT i;
        for (i = 0; i != index; ++i)
            cp = NEXT_STR(cp);

        REBSIZ actual = cast(REBYTE*, cp) - SER_DATA_RAW(SER(s));
        assert(actual == offset);
    }
#endif

// Note that we only ever create caches for strings that have had STR_AT()
// run on them.  So the more operations that avoid STR_AT(), the better!
// Using STR_HEAD() and STR_TAIL() will give a REBCHR(*) that can be used to
// iterate much faster, and most of the strings in the system might be able
// to get away with not having any bookmarks at all.
//
inline static REBCHR(*) STR_AT(REBSTR *s, REBCNT at) {
    assert(at <= STR_LEN(s));

    if (Is_Definitely_Ascii(s)) {  // can't have any false positives
        assert(not LINK(s).bookmarks);  // mutations must ensure this
        return cast(REBCHR(*), cast(REBYTE*, STR_HEAD(s)) + at);
    }

    REBCHR(*) cp;  // can be used to calculate offset (relative to STR_HEAD())
    REBCNT index;

    REBBMK *bookmark = nullptr;  // updated at end if not nulled out
    if (not IS_STR_SYMBOL(s))
        bookmark = LINK(s).bookmarks;

  #if defined(DEBUG_SPORADICALLY_DROP_BOOKMARKS)
    if (bookmark and SPORADICALLY(100)) {
        Free_Bookmarks_Maybe_Null(s);
        bookmark = nullptr;
    }
  #endif

    REBCNT len = STR_LEN(s);
    if (at < len / 2) {
        if (len < sizeof(REBVAL)) {
            if (not IS_STR_SYMBOL(s))
                assert(not bookmark);  // mutations must ensure this
            goto scan_from_head;  // good locality, avoid bookmark logic
        }
        if (not bookmark and not IS_STR_SYMBOL(s)) {
            LINK(s).bookmarks = bookmark = Alloc_Bookmark();
            goto scan_from_head;  // will fill in bookmark
        }
    }
    else {
        if (len < sizeof(REBVAL)) {
            if (not IS_STR_SYMBOL(s))
                assert(not bookmark);  // mutations must ensure this
            goto scan_from_tail;  // good locality, avoid bookmark logic
        }
        if (not bookmark and not IS_STR_SYMBOL(s)) {
            LINK(s).bookmarks = bookmark = Alloc_Bookmark();
            goto scan_from_tail;  // will fill in bookmark
        }
    }

    // Theoretically, a large UTF-8 string could have multiple "bookmarks".
    // That would complicate this logic by having to decide which one was
    // closest to be using.  For simplicity we just use one right now to
    // track the last access--which speeds up the most common case of an
    // iteration.  Improve as time permits!
    //
    assert(not LINK(bookmark).bookmarks);  // only one for now

  blockscope {
    REBCNT booked = BMK_INDEX(bookmark);

    if (at < booked / 2) {  // !!! when faster to seek from head?
        bookmark = nullptr;
        goto scan_from_head;
    }
    if (at > len - (booked / 2)) {  // !!! when faster to seek from tail?
        bookmark = nullptr;
        goto scan_from_tail;
    }

    index = booked;
    cp = cast(REBCHR(*), SER_DATA_RAW(SER(s)) + BMK_OFFSET(bookmark)); }

    if (index > at)
        goto scan_backward;

    goto scan_forward;

  scan_from_head:

    cp = STR_HEAD(s);
    index = 0;

  scan_forward:

    assert(index <= at);
    for (; index != at; ++index)
        cp = NEXT_STR(cp);

    if (not bookmark)
        return cp;

    goto update_bookmark;

  scan_from_tail:

    cp = STR_TAIL(s);
    index = len;

  scan_backward:

    assert(index >= at);
    for (; index != at; --index)
        cp = BACK_STR(cp);

    if (not bookmark)
        return cp;

  update_bookmark:

    BMK_INDEX(bookmark) = index;
    BMK_OFFSET(bookmark) = cp - STR_HEAD(s);

  #if defined(DEBUG_VERIFY_STR_AT)
    REBCHR(*) check_cp = STR_HEAD(s);
    REBCNT check_index = 0;
    for (; check_index != at; ++check_index)
        check_cp = NEXT_STR(check_cp);
    assert(check_cp == cp);
  #endif

    return cp;
}

#define VAL_STRING_HEAD(v) \
    STR_HEAD(VAL_SERIES(v))

#define VAL_STRING_TAIL(v) \
    STR_TAIL(VAL_SERIES(v))

inline static REBSTR *VAL_STRING(const REBCEL *v) {
    assert(ANY_STRING_KIND(CELL_KIND(v)) or ANY_WORD_KIND(CELL_KIND(v)));
    return STR(VAL_NODE(v));  // VAL_SERIES() would assert
}

inline static REBCNT VAL_LEN_HEAD(const REBCEL *v) {
    if (REB_BINARY == CELL_KIND(v))
        return SER_USED(VAL_SERIES(v));  // binaries can alias strings...
    return SER_LEN(VAL_SERIES(v));  // senses strings, not optimal.  :-/
}

inline static bool VAL_PAST_END(const REBCEL *v)
   { return VAL_INDEX(v) > VAL_LEN_HEAD(v); }

inline static REBCNT VAL_LEN_AT(const REBCEL *v) {
    //
    // !!! At present, it is considered "less of a lie" to tell people the
    // length of a series is 0 if its index is actually past the end, than
    // to implicitly clip the data pointer on out of bounds access.  It's
    // still going to be inconsistent, as if the caller extracts the index
    // and low level SER_LEN() themselves, they'll find it doesn't add up.
    // This is a longstanding historical Rebol issue that needs review.
    //
    if (VAL_INDEX(v) >= VAL_LEN_HEAD(v))
        return 0;  // avoid negative index

    return VAL_LEN_HEAD(v) - VAL_INDEX(v);  // take current index into account
}

inline static REBCHR(*) VAL_STRING_AT(const REBCEL *v) {
    REBSTR *s = VAL_STRING(v);  // debug build checks that it's ANY-STRING!
    if (VAL_INDEX(v) == 0)
        return STR_HEAD(s);  // common case, try and be fast
    if (VAL_PAST_END(v))
        fail (Error_Past_End_Raw());  // don't give deceptive return pointer
    return STR_AT(s, VAL_INDEX(v));
}

inline static REBSIZ VAL_SIZE_LIMIT_AT(
    REBCNT *length, // length in chars to end (including limit)
    const REBCEL *v,
    REBINT limit // -1 for no limit
){
    assert(ANY_STRING_KIND(CELL_KIND(v)));

    REBCHR(const*) at = VAL_STRING_AT(v); // !!! update cache if needed
    REBCHR(const*) tail;

    if (limit == -1) {
        if (length != NULL)
            *length = VAL_LEN_AT(v);
        tail = VAL_STRING_TAIL(v); // byte count known (fast)
    }
    else {
        if (length != NULL)
            *length = limit;
        tail = at;
        for (; limit > 0; --limit)
            tail = NEXT_STR(tail);
    }

    return tail - at;
}

#define VAL_SIZE_AT(v) \
    VAL_SIZE_LIMIT_AT(NULL, v, -1)

inline static REBSIZ VAL_OFFSET(const RELVAL *v) {
    return VAL_STRING_AT(v) - VAL_STRING_HEAD(v);
}

inline static REBSIZ VAL_OFFSET_FOR_INDEX(const REBCEL *v, REBCNT index) {
    assert(ANY_STRING_KIND(CELL_KIND(v)));

    REBCHR(const*) at;

    if (index == VAL_INDEX(v))
        at = VAL_STRING_AT(v); // !!! update cache if needed
    else if (index == VAL_LEN_HEAD(v))
        at = VAL_STRING_TAIL(v);
    else {
        // !!! arbitrary seeking...this technique needs to be tuned, e.g.
        // to look from the head or the tail depending on what's closer
        //
        at = STR_AT(VAL_STRING(v), index);
    }

    return at - VAL_STRING_HEAD(v);
}


//=//// INEFFICIENT SINGLE GET-AND-SET CHARACTER OPERATIONS //////////////=//
//
// These should generally be avoided by routines that are iterating, which
// should instead be using the REBCHR(*)-based APIs to maneuver through the
// UTF-8 data in a continuous way.
//
// !!! At time of writing, PARSE is still based on this method.  Instead, it
// should probably lock the input series against modification...or at least
// hold a cache that it throws away whenever it runs a GROUP!.

inline static REBUNI GET_CHAR_AT(REBSTR *s, REBCNT n) {
    REBCHR(const*) up = STR_AT(s, n);
    REBUNI c;
    NEXT_CHR(&c, up);
    return c;
}

inline static void SET_CHAR_AT(REBSTR *s, REBCNT n, REBUNI c) {
    assert(not IS_STR_SYMBOL(s));
    assert(n < STR_LEN(s));

    REBCHR(*) cp = STR_AT(s, n);

    // If the codepoint we are writing is the same size as the codepoint that
    // is already there, then we can just ues WRITE_CHR() and be done.
    //
    REBCNT size_old = 1 + trailingBytesForUTF8[*cast(REBYTE*, cp)];
    REBCNT size_new = Encoded_Size_For_Codepoint(c);
    if (size_new == size_old) {
        // common case... no memory shuffling needed
    }
    else if (size_old > size_new) {  // shuffle forward, not memcpy, overlaps!
        REBYTE *later = cast(REBYTE*, cp) + (size_old - size_new);
        memmove(cp, later, STR_TAIL(s) - later);  // not memcpy()!
    }
    else {  // need backward, may need series expansion, not memcpy, overlaps!
        EXPAND_SERIES_TAIL(SER(s), size_new - size_old);
        REBYTE *later = cast(REBYTE*, cp) + (size_new - size_old);
        memmove(cp, later, STR_TAIL(s) - later);  // not memcpy()!
    }

    WRITE_CHR(cp, c);
}

inline static REBCNT Num_Codepoints_For_Bytes(
    const REBYTE *start,
    const REBYTE *end
){
    assert(end >= start);
    REBCNT num_chars = 0;
    REBCHR(const*) cp = cast(REBCHR(const*), start);
    for (; cp != end; ++num_chars)
        cp = NEXT_STR(cp);
    return num_chars;
}


//=//// ANY-STRING! CONVENIENCE MACROS ////////////////////////////////////=//

#define Init_Any_String(v,t,s) \
    Init_Any_String_At_Core((v), (t), (s), 0)

#define Init_Text(v,s)      Init_Any_String((v), REB_TEXT, (s))
#define Init_File(v,s)      Init_Any_String((v), REB_FILE, (s))
#define Init_Email(v,s)     Init_Any_String((v), REB_EMAIL, (s))
#define Init_Tag(v,s)       Init_Any_String((v), REB_TAG, (s))
#define Init_Url(v,s)       Init_Any_String((v), REB_URL, (s))
#define Init_Issue(v,s)     Init_Any_String((v), REB_ISSUE, (s))



//=//// REBSTR CREATION HELPERS ///////////////////////////////////////////=//
//
// Note that most clients should be using the rebStringXXX() APIs for this
// and generate REBVAL*.  Note also that these routines may fail() if the
// data they are given is not UTF-8.

#define Make_String(encoded_capacity) \
    Make_String_Core((encoded_capacity), SERIES_FLAGS_NONE)

inline static REBSTR *Make_String_UTF8(const char *utf8) {
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Fail(NULL, utf8, strsize(utf8), crlf_to_lf);
}

inline static REBSTR *Make_Sized_String_UTF8(const char *utf8, size_t size) {
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Fail(NULL, utf8, size, crlf_to_lf);
}


//=//// REBSTR HASHING ////////////////////////////////////////////////////=//

inline static REBINT Hash_String(REBSTR *str)
    { return Hash_UTF8(STR_HEAD(str), STR_SIZE(str)); }

inline static REBINT First_Hash_Candidate_Slot(
    REBCNT *skip_out,
    REBCNT hash,
    REBCNT num_slots
){
    *skip_out = (hash & 0x0000FFFF) % num_slots;
    if (*skip_out == 0)
        *skip_out = 1;
    return (hash & 0x00FFFF00) % num_slots;
}


//=//// REBSTR COPY HELPERS ///////////////////////////////////////////////=//

#define Copy_String_At(v) \
    Copy_String_At_Limit((v), -1)

inline static REBSER *Copy_Sequence_At_Len(
    REBSER *s,
    REBCNT index,
    REBCNT len
){
    return Copy_Sequence_At_Len_Extra(s, index, len, 0);
}
