//
//  File: %sys-string.h
//  Summary: {Definitions for REBSTR (e.g. WORD!) and REBUNI (e.g. STRING!)}
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
// The ANY-STRING! and ANY-WORD! data types follow "UTF-8 everywhere", and
// store their content as UTF-8 at all times.  Then it only converts to other
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
//   See the distinction between SER_USED() and STR_LEN().
//


// Some places permit an optional label (such as the names of function
// invocations, which may not have an associated name).  To make the callsite
// intent clearer for passing in a null REBSTR*, use ANONYMOUS instead.
//
#define ANONYMOUS \
    cast(const REBSYM*, nullptr)


// For a writable REBSTR, a list of entities that cache the mapping from
// index to character offset is maintained.  Without some help, it would
// be necessary to search from the head or tail of the string, character
// by character, to turn an index into an offset.  This is prohibitive.
//
// These bookmarks must be kept in sync.  How many bookmarks are kept
// should be reigned in proportionally to the length of the series.  As
// a first try of this strategy, singular arrays are being used.
//
#define LINK_Bookmarks_TYPE     REBBMK*  // alias for REBSER* at this time
#define LINK_Bookmarks_CAST     (REBBMK*)SER
#define HAS_LINK_Bookmarks      FLAVOR_STRING


inline static REBCHR(*) NEXT_CHR(
    REBUNI *codepoint_out,
    REBCHR(const_if_unchecked_utf8*) cp
){
    const REBYTE *t = cp;
    if (*t < 0x80)
        *codepoint_out = *t;
    else
        t = Back_Scan_UTF8_Char_Unchecked(codepoint_out, t);
    return cast(REBCHR(*), m_cast(REBYTE*, t + 1));
}

inline static REBCHR(*) BACK_CHR(
    REBUNI *codepoint_out,
    REBCHR(const_if_unchecked_utf8*) cp
){
    const_if_unchecked_utf8 REBYTE *t = cp;
    --t;
    while (Is_Continuation_Byte_If_Utf8(*t))
        --t;
    NEXT_CHR(codepoint_out, cast(REBCHR(const_if_unchecked_utf8*), t));
    return cast(REBCHR(*), m_cast(REBYTE*, t));
}

inline static REBCHR(*) NEXT_STR(REBCHR(const_if_unchecked_utf8*) cp) {
    const_if_unchecked_utf8 REBYTE *t = cp;
    do {
        ++t;
    } while (Is_Continuation_Byte_If_Utf8(*t));
    return cast(REBCHR(*), m_cast(REBYTE*, t));
}

inline static REBCHR(*) BACK_STR(REBCHR(const_if_unchecked_utf8*) cp) {
    const_if_unchecked_utf8 REBYTE *t = cp;
    do {
        --t;
    } while (Is_Continuation_Byte_If_Utf8(*t));
    return cast(REBCHR(*), m_cast(REBYTE*, t));
}

inline static REBCHR(*) SKIP_CHR(
    REBUNI *codepoint_out,
    REBCHR(const_if_unchecked_utf8*) cp,
    REBINT delta
){
    if (delta > 0) {
        while (delta != 0) {
            cp = NEXT_STR(cp);
            --delta;
        }
    }
    else {
        while (delta != 0) {
            cp = BACK_STR(cp);
            ++delta;
        }
    }
    NEXT_CHR(codepoint_out, cp);
    return m_cast(REBCHR(*), cp);
}

#if DEBUG_UTF8_EVERYWHERE
    //
    // See the definition of `const_if_c` for the explanation of why this
    // overloading technique is needed to make output constness match input.
    //
    inline static REBCHR(const*) NEXT_CHR(
        REBUNI *codepoint_out,
        REBCHR(const*) cp
    ){
        return NEXT_CHR(codepoint_out, m_cast(REBCHR(*), cp));
    }

    inline static REBCHR(const*) BACK_CHR(
        REBUNI *codepoint_out,
        REBCHR(const*) cp
    ){
        return BACK_CHR(codepoint_out, m_cast(REBCHR(*), cp));
    }

    inline static REBCHR(const*) NEXT_STR(REBCHR(const*) cp)
      { return NEXT_STR(m_cast(REBCHR(*), cp)); }

    inline static REBCHR(const*) BACK_STR(REBCHR(const*) cp)
      { return BACK_STR(m_cast(REBCHR(*), cp)); }

    inline static REBCHR(const*) SKIP_CHR(
        REBUNI *codepoint_out,
        REBCHR(const*) cp,
        REBINT delta
    ){
        return SKIP_CHR(codepoint_out, m_cast(REBCHR(*), cp), delta);
    }
#endif

inline static REBUNI CHR_CODE(REBCHR(const*) cp) {
    REBUNI codepoint;
    NEXT_CHR(&codepoint, cp);
    return codepoint;
}

inline static REBCHR(*) WRITE_CHR(REBCHR(*) cp, REBUNI c) {
    REBSIZ size = Encoded_Size_For_Codepoint(c);
    Encode_UTF8_Char(cp, c, size);
    return cast(REBCHR(*), cast(REBYTE*, cp) + size);
}


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

inline static bool Is_String_Definitely_ASCII(const REBSTR *str) {
    UNUSED(str);
    return false;
}

#define STR_UTF8(s) \
    SER_HEAD(const char, ensure(const REBSTR*, s))

#define STR_SIZE(s) \
    SER_USED(ensure(const REBSTR*, s))  // UTF-8 byte count (not codepoints)

inline static REBCHR(*) STR_HEAD(const_if_c REBSTR *s)
  { return cast(REBCHR(*), SER_HEAD(REBYTE, s)); }

inline static REBCHR(*) STR_TAIL(const_if_c REBSTR *s)
  { return cast(REBCHR(*), SER_TAIL(REBYTE, s)); }

#if CPLUSPLUS_11
    inline static REBCHR(const*) STR_HEAD(const REBSTR *s)
      { return STR_HEAD(m_cast(REBSTR*, s)); }

    inline static REBCHR(const*) STR_TAIL(const REBSTR *s)
      { return STR_TAIL(m_cast(REBSTR*, s)); }
#endif


inline static REBLEN STR_LEN(const REBSTR *s) {
    if (Is_Definitely_Ascii(s))
        return STR_SIZE(s);

    if (IS_NONSYMBOL_STRING(s)) {  // length is cached for non-ANY-WORD!
      #if DEBUG_UTF8_EVERYWHERE
        if (s->misc.length > SER_USED(s))  // includes 0xDECAFBAD
            panic(s);
      #endif
        return s->misc.length;
    }

    // Have to do it the slow way if it's a symbol series...but hopefully
    // they're not too long (since spaces and newlines are illegal.)
    //
    REBLEN len = 0;
    REBCHR(const*) ep = STR_TAIL(s);
    REBCHR(const*) cp = STR_HEAD(s);
    while (cp != ep) {
        cp = NEXT_STR(cp);
        ++len;
    }
    return len;
}

inline static REBLEN STR_INDEX_AT(const REBSTR *s, REBSIZ offset) {
    if (Is_Definitely_Ascii(s))
        return offset;

    // The position `offset` describes must be a codepoint boundary.
    //
    assert(not Is_Continuation_Byte_If_Utf8(*BIN_AT(s, offset)));

    if (IS_NONSYMBOL_STRING(s)) {  // length is cached for non-ANY-WORD!
      #if DEBUG_UTF8_EVERYWHERE
        if (s->misc.length > SER_USED(s))  // includes 0xDECAFBAD
            panic(s);
      #endif

        // We have length and bookmarks.  We should build STR_AT() based on
        // this routine.  For now, fall through and do it slowly.
    }

    // Have to do it the slow way if it's a symbol series...but hopefully
    // they're not too long (since spaces and newlines are illegal.)
    //
    REBLEN index = 0;
    REBCHR(const*) ep = cast(REBCHR(const*), BIN_AT(s, offset));
    REBCHR(const*) cp = STR_HEAD(s);
    while (cp != ep) {
        cp = NEXT_STR(cp);
        ++index;
    }
    return index;
}

inline static void SET_STR_LEN_SIZE(REBSTR *s, REBLEN len, REBSIZ used) {
    assert(IS_NONSYMBOL_STRING(s));
    assert(len <= used);
    assert(used == SER_USED(s));
    s->misc.length = len;
    assert(*BIN_AT(s, used) == '\0');
    UNUSED(used);
}

inline static void TERM_STR_LEN_SIZE(REBSTR *s, REBLEN len, REBSIZ used) {
    assert(IS_NONSYMBOL_STRING(s));
    assert(len <= used);
    SET_SERIES_USED(s, used);
    s->misc.length = len;
    *BIN_AT(s, used) = '\0';
}


//=//// CACHED ACCESSORS AND BOOKMARKS ////////////////////////////////////=//
//
// A "bookmark" in this terminology is simply a REBSER which contains a list
// of indexes and offsets.  This helps to accelerate finding positions in
// UTF-8 strings based on index, vs. having to necessarily search from the
// beginning.
//
// !!! At the moment, only one bookmark is in effect at a time.  Even though
// it's just two numbers, there's only one pointer's worth of space in the
// series node otherwise.  Bookmarks aren't generated for strings that are
// very short, or that are never enumerated.

#define BMK_INDEX(b) \
    SER_HEAD(struct Reb_Bookmark, c_cast(REBBMK*, (b)))->index

#define BMK_OFFSET(b) \
    SER_HEAD(struct Reb_Bookmark, c_cast(REBBMK*, (b)))->offset

inline static REBBMK* Alloc_Bookmark(void) {
    REBSER *s = Make_Series(
        1,
        FLAG_FLAVOR(BOOKMARKLIST) | SERIES_FLAG_MANAGED
    );
    SET_SERIES_LEN(s, 1);
    CLEAR_SERIES_FLAG(s, MANAGED);  // manual but untracked (avoid leak error)
    return cast(REBBMK*, s);
}

inline static void Free_Bookmarks_Maybe_Null(REBSTR *str) {
    assert(IS_NONSYMBOL_STRING(str));
    if (LINK(Bookmarks, str)) {
        GC_Kill_Series(LINK(Bookmarks, str));
        mutable_LINK(Bookmarks, str) = nullptr;
    }
}

#if !defined(NDEBUG)
    inline static void Check_Bookmarks_Debug(REBSTR *s) {
        REBBMK *bookmark = LINK(Bookmarks, s);
        if (not bookmark)
            return;

        REBLEN index = BMK_INDEX(bookmark);
        REBSIZ offset = BMK_OFFSET(bookmark);

        REBCHR(*) cp = STR_HEAD(s);
        REBLEN i;
        for (i = 0; i != index; ++i)
            cp = NEXT_STR(cp);

        REBSIZ actual = cast(REBYTE*, cp) - SER_DATA(s);
        assert(actual == offset);
    }
#endif

// The caching strategy of UTF-8 Everywhere is fairly experimental, and it
// helps to be able to debug it.  Currently it is selectively debuggable when
// callgrind is enabled, as part of performance analysis.
//
#if DEBUG_TRACE_BOOKMARKS
    #define BOOKMARK_TRACE(...)  /* variadic, requires at least C99 */ \
        do { if (PG_Callgrind_On) { \
            printf("/ ");  /* separate sections (spare leading /) */ \
            printf(__VA_ARGS__); \
        } } while (0)
#endif

// Note that we only ever create caches for strings that have had STR_AT()
// run on them.  So the more operations that avoid STR_AT(), the better!
// Using STR_HEAD() and STR_TAIL() will give a REBCHR(*) that can be used to
// iterate much faster, and most of the strings in the system might be able
// to get away with not having any bookmarks at all.
//
inline static REBCHR(*) STR_AT(const_if_c REBSTR *s, REBLEN at) {
    assert(at <= STR_LEN(s));

    if (Is_Definitely_Ascii(s)) {  // can't have any false positives
        assert(not LINK(Bookmarks, s));  // mutations must ensure this
        return cast(REBCHR(*), cast(REBYTE*, STR_HEAD(s)) + at);
    }

    REBCHR(*) cp;  // can be used to calculate offset (relative to STR_HEAD())
    REBLEN index;

    REBBMK *bookmark = nullptr;  // updated at end if not nulled out
    if (IS_NONSYMBOL_STRING(s))
        bookmark = LINK(Bookmarks, s);

  #if DEBUG_SPORADICALLY_DROP_BOOKMARKS
    if (bookmark and SPORADICALLY(100)) {
        Free_Bookmarks_Maybe_Null(s);
        bookmark = nullptr;
    }
  #endif

    REBLEN len = STR_LEN(s);

  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("len %ld @ %ld ", len, at);
    BOOKMARK_TRACE("%s", bookmark ? "bookmarked" : "no bookmark");
  #endif

    if (at < len / 2) {
        if (len < sizeof(REBVAL)) {
            if (IS_NONSYMBOL_STRING(s))
                assert(
                    GET_SERIES_FLAG(s, DYNAMIC)  // e.g. mold buffer
                    or not bookmark  // mutations must ensure this
                );
            goto scan_from_head;  // good locality, avoid bookmark logic
        }
        if (not bookmark and IS_NONSYMBOL_STRING(s)) {
            bookmark = Alloc_Bookmark();
            mutable_LINK(Bookmarks, m_cast(REBSTR*, s)) = bookmark;
            goto scan_from_head;  // will fill in bookmark
        }
    }
    else {
        if (len < sizeof(REBVAL)) {
            if (IS_NONSYMBOL_STRING(s))
                assert(
                    not bookmark  // mutations must ensure this usually but...
                    or GET_SERIES_FLAG(s, DYNAMIC)  // !!! mold buffer?
                );
            goto scan_from_tail;  // good locality, avoid bookmark logic
        }
        if (not bookmark and IS_NONSYMBOL_STRING(s)) {
            bookmark = Alloc_Bookmark();
            mutable_LINK(Bookmarks, m_cast(REBSTR*, s)) = bookmark;
            goto scan_from_tail;  // will fill in bookmark
        }
    }

    // Theoretically, a large UTF-8 string could have multiple "bookmarks".
    // That would complicate this logic by having to decide which one was
    // closest to be using.  For simplicity we just use one right now to
    // track the last access--which speeds up the most common case of an
    // iteration.  Improve as time permits!
    //
    assert(not bookmark or SER_USED(bookmark) == 1);  // only one

  blockscope {
    REBLEN booked = bookmark ? BMK_INDEX(bookmark) : 0;

    // `at` is always positive.  `booked - at` may be negative, but if it
    // is positive and bigger than `at`, faster to seek from head.
    //
    if (cast(REBINT, at) < cast(REBINT, booked) - cast(REBINT, at)) {
        if (booked > sizeof(REBVAL))
            bookmark = nullptr;  // don't throw away bookmark for low searches
        goto scan_from_head;
    }

    // `len - at` is always positive.  `at - booked` may be negative, but if
    // it is positive and bigger than `len - at`, faster to seek from tail.
    //
    if (cast(REBINT, len - at) < cast(REBINT, at) - cast(REBINT, booked)) {
        if (booked > sizeof(REBVAL))
            bookmark = nullptr;  // don't throw away bookmark for low searches
        goto scan_from_tail;
    }

    index = booked;
    if (bookmark)
        cp = cast(REBCHR(*), SER_DATA(s) + BMK_OFFSET(bookmark));
    else
        cp = cast(REBCHR(*), SER_DATA(s));
  }

    if (index > at) {
      #if DEBUG_TRACE_BOOKMARKS
        BOOKMARK_TRACE("backward scan %ld", index - at);
      #endif
        goto scan_backward;
    }

  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("forward scan %ld", at - index);
  #endif
    goto scan_forward;

  scan_from_head:
  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("scan from head");
  #endif
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
  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("scan from tail");
  #endif
    cp = STR_TAIL(s);
    index = len;

  scan_backward:
    assert(index >= at);
    for (; index != at; --index)
        cp = BACK_STR(cp);

    if (not bookmark) {
      #if DEBUG_TRACE_BOOKMARKS
        BOOKMARK_TRACE("not cached\n");
      #endif
        return cp;
    }

  update_bookmark:
  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("caching %ld\n", index);
  #endif
    BMK_INDEX(bookmark) = index;
    BMK_OFFSET(bookmark) = cp - STR_HEAD(s);

  #if DEBUG_VERIFY_STR_AT
    REBCHR(*) check_cp = STR_HEAD(s);
    REBLEN check_index = 0;
    for (; check_index != at; ++check_index)
        check_cp = NEXT_STR(check_cp);
    assert(check_cp == cp);
  #endif

    return cp;
}

#if CPLUSPLUS_11
    inline static REBCHR(const*) STR_AT(const REBSTR *s, REBLEN at)
      { return STR_AT(m_cast(REBSTR*, s), at); }
#endif


inline static const REBSTR *VAL_STRING(noquote(const Cell*) v) {
    if (ANY_STRING_KIND(CELL_HEART(v)))
        return STR(VAL_NODE1(v));  // VAL_SERIES() would assert

    return VAL_WORD_SYMBOL(v);  // asserts ANY_WORD_KIND() for heart
}

#define VAL_STRING_ENSURE_MUTABLE(v) \
    m_cast(REBSTR*, VAL_STRING(ENSURE_MUTABLE(v)))

// This routine works with the notion of "length" that corresponds to the
// idea of the datatype which the series index is for.  Notably, a BINARY!
// can alias an ANY-STRING! or ANY-WORD! and address the individual bytes of
// that type.  So if the series is a string and not a binary, the special
// cache of the length in the series node for strings must be used.
//
inline static REBLEN VAL_LEN_HEAD(noquote(const Cell*) v) {
    const REBSER *s = VAL_SERIES(v);
    if (IS_SER_UTF8(s) and CELL_KIND(v) != REB_BINARY)
        return STR_LEN(STR(s));
    return SER_USED(s);
}

inline static bool VAL_PAST_END(noquote(const Cell*) v)
   { return VAL_INDEX(v) > VAL_LEN_HEAD(v); }

inline static REBLEN VAL_LEN_AT(noquote(const Cell*) v) {
    //
    // !!! At present, it is considered "less of a lie" to tell people the
    // length of a series is 0 if its index is actually past the end, than
    // to implicitly clip the data pointer on out of bounds access.  It's
    // still going to be inconsistent, as if the caller extracts the index
    // and low level length themselves, they'll find it doesn't add up.
    // This is a longstanding historical Rebol issue that needs review.
    //
    REBIDX i = VAL_INDEX(v);
    if (i > cast(REBIDX, VAL_LEN_HEAD(v)))
        fail ("Index past end of series");
    if (i < 0)
        fail ("Index before beginning of series");

    return VAL_LEN_HEAD(v) - i;  // take current index into account
}

inline static REBCHR(const*) VAL_STRING_AT(noquote(const Cell*) v) {
    const REBSTR *str = VAL_STRING(v);  // checks that it's ANY-STRING!
    REBIDX i = VAL_INDEX_RAW(v);
    REBLEN len = STR_LEN(str);
    if (i < 0 or i > cast(REBIDX, len))
        fail (Error_Index_Out_Of_Range_Raw());
    return i == 0 ? STR_HEAD(str) : STR_AT(str, i);
}


inline static REBCHR(const*) VAL_STRING_TAIL(noquote(const Cell*) v) {
    const REBSTR *s = VAL_STRING(v);  // debug build checks it's ANY-STRING!
    return STR_TAIL(s);
}



#define VAL_STRING_AT_ENSURE_MUTABLE(v) \
    m_cast(REBCHR(*), VAL_STRING_AT(ENSURE_MUTABLE(v)))

#define VAL_STRING_AT_KNOWN_MUTABLE(v) \
    m_cast(REBCHR(*), VAL_STRING_AT(KNOWN_MUTABLE(v)))


inline static REBSIZ VAL_SIZE_LIMIT_AT(
    option(REBLEN*) length_out,  // length in chars to end (including limit)
    noquote(const Cell*) v,
    REBINT limit  // UNLIMITED (e.g. a very large number) for no limit
){
    assert(ANY_STRING_KIND(CELL_HEART(v)));

    REBCHR(const*) at = VAL_STRING_AT(v);  // !!! update cache if needed
    REBCHR(const*) tail;

    REBLEN len_at = VAL_LEN_AT(v);
    if (cast(REBLEN, limit) >= len_at) {  // UNLIMITED casts to large unsigned
        if (length_out)
            *unwrap(length_out) = len_at;
        tail = VAL_STRING_TAIL(v);  // byte count known (fast)
    }
    else {
        assert(limit >= 0);
        if (length_out)
            *unwrap(length_out) = limit;
        tail = at;
        for (; limit > 0; --limit)
            tail = NEXT_STR(tail);
    }

    return tail - at;
}

#define VAL_SIZE_AT(v) \
    VAL_SIZE_LIMIT_AT(nullptr, v, UNLIMITED)

inline static REBSIZ VAL_OFFSET(const RELVAL *v) {
    return VAL_STRING_AT(v) - STR_HEAD(VAL_STRING(v));
}

inline static REBSIZ VAL_OFFSET_FOR_INDEX(noquote(const Cell*) v, REBLEN index) {
    assert(ANY_STRING_KIND(CELL_HEART(v)));

    REBCHR(const*) at;

    if (index == VAL_INDEX(v))
        at = VAL_STRING_AT(v); // !!! update cache if needed
    else if (index == VAL_LEN_HEAD(v))
        at = STR_TAIL(VAL_STRING(v));
    else {
        // !!! arbitrary seeking...this technique needs to be tuned, e.g.
        // to look from the head or the tail depending on what's closer
        //
        at = STR_AT(VAL_STRING(v), index);
    }

    return at - STR_HEAD(VAL_STRING(v));
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

inline static REBUNI GET_CHAR_AT(const REBSTR *s, REBLEN n) {
    REBCHR(const*) up = STR_AT(s, n);
    REBUNI c;
    NEXT_CHR(&c, up);
    return c;
}


// !!! This code is a subset of what Modify_String() can also handle.  Having
// it is an optimization that may-or-may-not be worth the added complexity of
// having more than one way of doing a CHANGE to a character.  Review.
//
inline static void SET_CHAR_AT(REBSTR *s, REBLEN n, REBUNI c) {
    //
    // We are maintaining the same length, but DEBUG_UTF8_EVERYWHERE will
    // corrupt the length every time the SER_USED() changes.  Workaround that
    // by saving the length and restoring at the end.
    //
  #if DEBUG_UTF8_EVERYWHERE
    REBLEN len = STR_LEN(s);
  #endif

    assert(IS_NONSYMBOL_STRING(s));
    assert(n < STR_LEN(s));

    REBCHR(*) cp = STR_AT(s, n);
    REBCHR(*) old_next_cp = NEXT_STR(cp);  // scans fast (for leading bytes)

    REBSIZ size = Encoded_Size_For_Codepoint(c);
    REBSIZ old_size = old_next_cp - cp;
    if (size == old_size) {
        // common case... no memory shuffling needed, no bookmarks need
        // to be updated.
    }
    else {
        size_t cp_offset = cp - STR_HEAD(s);  // for updating bookmark, expand

        int delta = size - old_size;
        if (delta < 0) {  // shuffle forward, memmove() vs memcpy(), overlaps!
            memmove(
                cast(REBYTE*, cp) + size,
                old_next_cp,
                STR_TAIL(s) - old_next_cp
            );

            SET_SERIES_USED(s, SER_USED(s) + delta);
        }
        else {
            EXPAND_SERIES_TAIL(s, delta);  // this adds to SERIES_USED
            cp = cast(REBCHR(*),  // refresh `cp` (may've reallocated!)
                cast(REBYTE*, STR_HEAD(s)) + cp_offset
            );
            REBYTE *later = cast(REBYTE*, cp) + delta;
            memmove(
                later,
                cp,
                cast(REBYTE*, STR_TAIL(s)) - later
            );  // Note: may not be terminated
        }

        *cast(REBYTE*, STR_TAIL(s)) = '\0';  // add terminator

        // `cp` still is the start of the character for the index we were
        // dealing with.  Only update bookmark if it's an offset *after*
        // that character position...
        //
        REBBMK *book = LINK(Bookmarks, s);
        if (book and BMK_OFFSET(book) > cp_offset)
            BMK_OFFSET(book) += delta;
    }

  #if DEBUG_UTF8_EVERYWHERE  // see note on `len` at start of function
    s->misc.length = len;
  #endif

    Encode_UTF8_Char(cp, c, size);
    ASSERT_SERIES_TERM_IF_NEEDED(s);
}

inline static REBLEN Num_Codepoints_For_Bytes(
    const REBYTE *start,
    const REBYTE *end
){
    assert(end >= start);
    REBLEN num_chars = 0;
    REBCHR(const*) cp = cast(REBCHR(const*), start);
    for (; cp != end; ++num_chars)
        cp = NEXT_STR(cp);
    return num_chars;
}


//=//// ANY-STRING! CONVENIENCE MACROS ////////////////////////////////////=//
//
// Declaring as inline with type signature ensures you use a REBSTR* to
// initialize, and the C++ build can also validate managed consistent w/const.

inline static REBVAL *Init_Any_String_At(
    RELVAL *out,
    enum Reb_Kind kind,
    const_if_c REBSTR *str,
    REBLEN index
){
    Init_Any_Series_At_Core(
        out,
        kind,
        Force_Series_Managed_Core(str),
        index,
        UNBOUND
    );
    if (kind == REB_URL)
        mutable_HEART_BYTE(out) = REB_TEXT;
    return SPECIFIC(out);
}

#if CPLUSPLUS_11
    inline static REBVAL *Init_Any_String_At(
        RELVAL *out,
        enum Reb_Kind kind,
        const REBSTR *str,
        REBLEN index
    ){
        return Init_Any_Series_At_Core(out, kind, str, index, UNBOUND);
    }
#endif

#define Init_Any_String(v,t,s) \
    Init_Any_String_At((v), (t), (s), 0)

#define Init_Text(v,s)      Init_Any_String((v), REB_TEXT, (s))
#define Init_File(v,s)      Init_Any_String((v), REB_FILE, (s))
#define Init_Email(v,s)     Init_Any_String((v), REB_EMAIL, (s))
#define Init_Tag(v,s)       Init_Any_String((v), REB_TAG, (s))
#define Init_Url(v,s)       Init_Any_String((v), REB_URL, (s))


//=//// REBSTR CREATION HELPERS ///////////////////////////////////////////=//
//
// Note that most clients should be using the rebStringXXX() APIs for this
// and generate REBVAL*.  Note also that these routines may fail() if the
// data they are given is not UTF-8.

#define Make_String(encoded_capacity) \
    Make_String_Core((encoded_capacity), SERIES_FLAGS_NONE)

inline static REBSTR *Make_String_UTF8(const char *utf8) {
    return Append_UTF8_May_Fail(NULL, utf8, strsize(utf8), STRMODE_NO_CR);
}

inline static REBSTR *Make_Sized_String_UTF8(const char *utf8, size_t size) {
    return Append_UTF8_May_Fail(NULL, utf8, size, STRMODE_NO_CR);
}


//=//// GLOBAL STRING CONSTANTS //////////////////////////////////////////=//

#define EMPTY_TEXT \
    Root_Empty_Text


//=//// REBSTR HASHING ////////////////////////////////////////////////////=//

inline static REBINT Hash_String(const REBSTR *str)
    { return Hash_UTF8_Len_Caseless(STR_HEAD(str), STR_LEN(str)); }

inline static REBINT First_Hash_Candidate_Slot(
    REBLEN *skip_out,
    REBLEN hash,
    REBLEN num_slots
){
    *skip_out = (hash & 0x0000FFFF) % num_slots;
    if (*skip_out == 0)
        *skip_out = 1;
    return (hash & 0x00FFFF00) % num_slots;
}


//=//// REBSTR COPY HELPERS ///////////////////////////////////////////////=//

#define Copy_String_At(v) \
    Copy_String_At_Limit((v), -1)

inline static REBSER *Copy_Binary_At_Len(
    const REBSER *s,
    REBLEN index,
    REBLEN len
){
    return Copy_Series_At_Len_Extra(
        s,
        index,
        len,
        0,
        FLAG_FLAVOR(BINARY) | SERIES_FLAGS_NONE
    );
}


// Conveying the part of a string which contains a CR byte is helpful.  But
// we may see this CR during a scan...e.g. the bytes that come after it have
// not been checked to see if they are valid UTF-8.  We assume all the bytes
// *prior* are known to be valid.
//
inline static REBCTX *Error_Illegal_Cr(const REBYTE *at, const REBYTE *start)
{
    assert(*at == CR);
    REBLEN back_len = 0;
    REBCHR(const*) back = cast(REBCHR(const*), at);
    while (back_len < 41 and back != start) {
        back = BACK_STR(back);
        ++back_len;
    }
    REBVAL *str = rebSizedText(
        cast(const char*, back),
        at - cast(const REBYTE*, back) + 1  // include CR (escaped, e.g. ^M)
    );
    REBCTX *error = Error_Illegal_Cr_Raw(str);
    rebRelease(str);
    return error;
}


// This routine is formulated in a way to try and share it in order to not
// repeat code for implementing Reb_Strmode many places.  See notes there.
//
inline static bool Should_Skip_Ascii_Byte_May_Fail(
    const REBYTE *bp,
    enum Reb_Strmode strmode,
    const REBYTE *start  // need for knowing how far back for error context
){
    if (*bp == '\0')
        fail (Error_Illegal_Zero_Byte_Raw());  // never allow #{00} in strings

    if (*bp == CR) {
        switch (strmode) {
          case STRMODE_ALL_CODEPOINTS:
            break;  // let the CR slide

          case STRMODE_CRLF_TO_LF: {
            if (bp[1] == LF)
                return true;  // skip the CR and get the LF as next character
            goto strmode_no_cr; }  // don't allow e.g. CR CR

          case STRMODE_NO_CR:
          strmode_no_cr:
            fail (Error_Illegal_Cr(bp, start));

          case STRMODE_LF_TO_CRLF:
            assert(!"STRMODE_LF_TO_CRLF handled by exporting routines only");
            break;
        }
    }

    return false;  // character is okay for string, don't skip
}

#define Validate_Ascii_Byte(bp,strmode,start) \
    cast(void, Should_Skip_Ascii_Byte_May_Fail((bp), (strmode), (start)))

#define Append_String(dest,string) \
    Append_String_Limit((dest), (string), UNLIMITED)
