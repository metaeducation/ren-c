//
//  File: %stub-string.h
//  Summary: {Definitions for REBSTR (e.g. WORD!) and Codepoint (e.g. STRING!)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// The ANY-STRING? and ANY-WORD? data types follow "UTF-8 everywhere", and
// store their content as UTF-8 at all times.  Then it only converts to other
// encodings at I/O points if the platform requires it (e.g. Windows):
//
// http://utf8everywhere.org/
//
// UTF-8 cannot in the general case provide O(1) access for indexing.  We
// attack the problem three ways:
//
// * Avoiding loops which try to access by index, and instead make it easier
//   to smoothly traverse known good UTF-8 data using Utf8(*).
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
//   See the distinction between Series_Used() and String_Len().
//


// For a writable REBSTR, a list of entities that cache the mapping from
// index to character offset is maintained.  Without some help, it would
// be necessary to search from the head or tail of the string, character
// by character, to turn an index into an offset.  This is prohibitive.
//
// These bookmarks must be kept in sync.  How many bookmarks are kept
// should be reigned in proportionally to the length of the series.  As
// a first try of this strategy, singular arrays are being used.
//
#define LINK_Bookmarks_TYPE     BookmarkList*  // alias for Series for now
#define HAS_LINK_Bookmarks      FLAVOR_STRING


INLINE Utf8(*) Skip_Codepoint(Utf8(const_if_c*) utf8) {
    Byte* bp = x_cast(Byte*, utf8);
    do {
        ++bp;
    } while (Is_Continuation_Byte(*bp));
    return cast(Utf8(*), bp);
}

INLINE Utf8(*) Step_Back_Codepoint(Utf8(const_if_c*) utf8) {
    Byte* bp = x_cast(Byte*, utf8);
    do {
        --bp;
    } while (Is_Continuation_Byte(*bp));
    return cast(Utf8(*), bp);
}

INLINE Utf8(*) Utf8_Next(
    Codepoint* codepoint_out,
    Utf8(const_if_c*) utf8
){
    Byte* bp = x_cast(Byte*, utf8);
    if (*bp < 0x80)
        *codepoint_out = *bp;
    else
        bp = m_cast(Byte*, Back_Scan_UTF8_Char_Unchecked(codepoint_out, bp));
    return cast(Utf8(*), bp + 1);  // see definition of Back_Scan() for why +1
}

INLINE Utf8(*) Utf8_Back(
    Codepoint* codepoint_out,
    Utf8(const_if_c*) utf8
){
    Byte* bp = x_cast(Byte*, utf8);
    --bp;
    while (Is_Continuation_Byte(*bp))
        --bp;
    Utf8_Next(codepoint_out, cast(Utf8(*), bp));
    return cast(Utf8(*), bp);
}

INLINE Utf8(*) Utf8_Skip(
    Codepoint* codepoint_out,
    Utf8(const_if_c*) utf8,
    REBINT delta
){
    if (delta > 0) {
        while (delta != 0) {
            utf8 = Skip_Codepoint(utf8);
            --delta;
        }
    }
    else {
        while (delta != 0) {
            utf8 = Step_Back_Codepoint(utf8);
            ++delta;
        }
    }
    Utf8_Next(codepoint_out, utf8);
    return m_cast(Utf8(*), utf8);
}

#if CPLUSPLUS_11
    // See the definition of `const_if_c` for the explanation of why this
    // overloading technique is needed to make output constness match input.

    INLINE Utf8(const*) Skip_Codepoint(Utf8(const*) utf8)
      { return Skip_Codepoint(m_cast(Utf8(*), utf8)); }

    INLINE Utf8(const*) Step_Back_Codepoint(Utf8(const*) utf8)
      { return Step_Back_Codepoint(m_cast(Utf8(*), utf8)); }

    INLINE Utf8(const*) Utf8_Next(
        Codepoint* codepoint_out,
        Utf8(const*) utf8
    ){
        return Utf8_Next(codepoint_out, m_cast(Utf8(*), utf8));
    }

    INLINE Utf8(const*) Utf8_Back(
        Codepoint* codepoint_out,
        Utf8(const*) utf8
    ){
        return Utf8_Back(codepoint_out, m_cast(Utf8(*), utf8));
    }

    INLINE Utf8(const*) Utf8_Skip(
        Codepoint* codepoint_out,
        Utf8(const*) utf8,
        REBINT delta
    ){
        return Utf8_Skip(codepoint_out, m_cast(Utf8(*), utf8), delta);
    }
#endif

INLINE Codepoint Codepoint_At(Utf8(const*) utf8) {
    Codepoint codepoint;
    Utf8_Next(&codepoint, utf8);
    return codepoint;
}

INLINE Utf8(*) Write_Codepoint(Utf8(*) utf8, Codepoint c) {
    Size size = Encoded_Size_For_Codepoint(c);
    Encode_UTF8_Char(utf8, c, size);
    return cast(Utf8(*), cast(Byte*, utf8) + size);
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

INLINE bool Is_String_Definitely_ASCII(const String* str) {
    UNUSED(str);
    return false;
}

#define String_UTF8(s)      Series_Head(char, ensure(const String*, s))
#define String_Head(s)      c_cast(Utf8(*), Series_Head(Byte, s))
#define String_Tail(s)      c_cast(Utf8(*), Series_Tail(Byte, s))

#define String_Size(s) \
    Series_Used(ensure(const String*, s))  // encoded byte size, not codepoints

#define String_Dynamic_Size(s) \
    Series_Dynamic_Used(ensure(const String*, s))

INLINE Length String_Len(const String* s) {
    if (not Is_String_Symbol(s)) {
      #if DEBUG_UTF8_EVERYWHERE
        if (s->misc.length > Series_Used(s))  // includes 0xDECAFBAD
            panic(s);
        if (Is_Definitely_Ascii(s))
            assert(s->misc.length == String_Size(s));
      #endif
        return s->misc.length;  // length cached in misc for non-ANY-WORD?
    }

    if (Is_Definitely_Ascii(s))
        return String_Size(s);

    Length len = 0;  // no length cache; hope symbol is short!
    Utf8(const*) ep = String_Tail(s);
    Utf8(const*) cp = String_Head(s);
    while (cp != ep) {
        cp = Skip_Codepoint(cp);
        ++len;
    }
    return len;
}

INLINE REBLEN String_Index_At(
    const String* s,
    Size byteoffset  // offset must be at an encoded codepoint start
){
    if (Is_Definitely_Ascii(s))
        return byteoffset;

    assert(not Is_Continuation_Byte(*Binary_At(s, byteoffset)));

    if (Is_String_NonSymbol(s)) {  // length is cached for non-ANY-WORD?
      #if DEBUG_UTF8_EVERYWHERE
        if (s->misc.length > Series_Used(s))  // includes 0xDECAFBAD
            panic(s);
      #endif

        // We have length and bookmarks.  We should build String_At() based on
        // this routine.  For now, fall through and do it slowly.
    }

    // Have to do it the slow way if it's a symbol series...but hopefully
    // they're not too long (since spaces and newlines are illegal.)
    //
    REBLEN index = 0;
    Utf8(const*) ep = cast(Utf8(const*), Binary_At(s, byteoffset));
    Utf8(const*) cp = String_Head(s);
    while (cp != ep) {
        cp = Skip_Codepoint(cp);
        ++index;
    }
    return index;
}

INLINE void Set_String_Len_Size(String* s, Length len, Size used) {
    assert(Is_String_NonSymbol(s));
    assert(len <= used);
    assert(used == Series_Used(s));
    s->misc.length = len;
    assert(*Binary_At(s, used) == '\0');
    UNUSED(used);
}

INLINE void Term_String_Len_Size(String* s, Length len, Size used) {
    assert(Is_String_NonSymbol(s));
    assert(len <= used);
    Set_Series_Used(s, used);
    s->misc.length = len;
    *Binary_At(s, used) = '\0';
}


//=//// CACHED ACCESSORS AND BOOKMARKS ////////////////////////////////////=//
//
// BookMarkList in this terminology is simply a series which contains a list
// of indexes and offsets.  This helps to accelerate finding positions in
// UTF-8 strings based on index, vs. having to necessarily search from the
// beginning.
//
// !!! At the moment, only one bookmark is in effect at a time.  Even though
// it's just two numbers, there's only one pointer's worth of space in the
// series node otherwise.  Bookmarks aren't generated for strings that are
// very short, or that are never enumerated.

#define BMK_INDEX(b) \
    Series_Head(Bookmark, (b))->index

#define BMK_OFFSET(b) \
    Series_Head(Bookmark, (b))->offset

INLINE BookmarkList* Alloc_BookmarkList(void) {
    BookmarkList* books = Make_Series(BookmarkList,
        1,
        FLAG_FLAVOR(BOOKMARKLIST)
            | NODE_FLAG_MANAGED  // lie to be untracked
    );
    Clear_Node_Managed_Bit(books);  // untracked and indefinite lifetime
    Set_Series_Len(books, 1);
    return books;
}

INLINE void Free_Bookmarks_Maybe_Null(String* str) {
    assert(Is_String_NonSymbol(str));
    if (LINK(Bookmarks, str)) {
        GC_Kill_Series(LINK(Bookmarks, str));
        LINK(Bookmarks, str) = nullptr;
    }
}

#if !defined(NDEBUG)
    INLINE void Check_Bookmarks_Debug(String* s) {
        BookmarkList* book = LINK(Bookmarks, s);
        if (not book)
            return;

        REBLEN index = BMK_INDEX(book);
        Size offset = BMK_OFFSET(book);

        Utf8(*) cp = String_Head(s);
        REBLEN i;
        for (i = 0; i != index; ++i)
            cp = Skip_Codepoint(cp);

        Size actual = cast(Byte*, cp) - Series_Data(s);
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

#if CPLUSPLUS_11
    INLINE Utf8(const*) String_At(const String* s, REBLEN at)
      { return String_At(m_cast(String*, s), at); }
#endif


//=//// INEFFICIENT SINGLE GET-AND-SET CHARACTER OPERATIONS //////////////=//
//
// These should generally be avoided by routines that are iterating, which
// should instead be using the Utf8(*)-based APIs to maneuver through the
// UTF-8 data in a continuous way.
//
// !!! At time of writing, PARSE is still based on this method.  Instead, it
// should probably lock the input series against modification...or at least
// hold a cache that it throws away whenever it runs a GROUP!.

INLINE Codepoint Get_Char_At(const String* s, REBLEN n) {
    Utf8(const*) up = String_At(s, n);
    Codepoint c;
    Utf8_Next(&c, up);
    return c;
}


// !!! This code is a subset of what Modify_String() can also handle.  Having
// it is an optimization that may-or-may-not be worth the added complexity of
// having more than one way of doing a CHANGE to a character.  Review.
//
INLINE void Set_Char_At(String* s, REBLEN n, Codepoint c) {
    //
    // We are maintaining the same length, but DEBUG_UTF8_EVERYWHERE will
    // corrupt the length every time the Series_Used() changes.  Workaround that
    // by saving the length and restoring at the end.
    //
  #if DEBUG_UTF8_EVERYWHERE
    REBLEN len = String_Len(s);
  #endif

    assert(Is_String_NonSymbol(s));
    assert(n < String_Len(s));

    Utf8(*) cp = String_At(s, n);
    Utf8(*) old_next_cp = Skip_Codepoint(cp);  // scans fast (for leading bytes)

    Size size = Encoded_Size_For_Codepoint(c);
    Size old_size = old_next_cp - cp;
    if (size == old_size) {
        // common case... no memory shuffling needed, no bookmarks need
        // to be updated.
    }
    else {
        size_t cp_offset = cp - String_Head(s);  // for updating bookmark, expand

        int delta = size - old_size;
        if (delta < 0) {  // shuffle forward, memmove() vs memcpy(), overlaps!
            memmove(
                cast(Byte*, cp) + size,
                old_next_cp,
                String_Tail(s) - old_next_cp
            );

            Set_Series_Used(s, Series_Used(s) + delta);
        }
        else {
            Expand_Series_Tail(s, delta);  // this adds to SERIES_USED
            cp = cast(Utf8(*),  // refresh `cp` (may've reallocated!)
                cast(Byte*, String_Head(s)) + cp_offset
            );
            Byte* later = cast(Byte*, cp) + delta;
            memmove(
                later,
                cp,
                cast(Byte*, String_Tail(s)) - later
            );  // Note: may not be terminated
        }

        *cast(Byte*, String_Tail(s)) = '\0';  // add terminator

        // `cp` still is the start of the character for the index we were
        // dealing with.  Only update bookmark if it's an offset *after*
        // that character position...
        //
        BookmarkList* book = LINK(Bookmarks, s);
        if (book and BMK_OFFSET(book) > cp_offset)
            BMK_OFFSET(book) += delta;
    }

  #if DEBUG_UTF8_EVERYWHERE  // see note on `len` at start of function
    s->misc.length = len;
  #endif

    Encode_UTF8_Char(cp, c, size);
    Assert_Series_Term_If_Needed(s);
}

INLINE REBLEN Num_Codepoints_For_Bytes(
    const Byte* start,
    const Byte* end
){
    assert(end >= start);
    REBLEN num_chars = 0;
    Utf8(const*) cp = cast(Utf8(const*), start);
    for (; cp != end; ++num_chars)
        cp = Skip_Codepoint(cp);
    return num_chars;
}


//=//// REBSTR CREATION HELPERS ///////////////////////////////////////////=//
//
// Note that most clients should be using the rebStringXXX() APIs for this
// and generate Value*.  Note also that these routines may fail() if the
// data they are given is not UTF-8.

#define Make_String(encoded_capacity) \
    Make_String_Core((encoded_capacity), SERIES_FLAGS_NONE)

INLINE String* Make_String_UTF8(const char *utf8) {
    return Append_UTF8_May_Fail(nullptr, utf8, strsize(utf8), STRMODE_NO_CR);
}

INLINE String* Make_Sized_String_UTF8(const char *utf8, size_t size) {
    return Append_UTF8_May_Fail(nullptr, utf8, size, STRMODE_NO_CR);
}


//=//// REBSTR HASHING ////////////////////////////////////////////////////=//

INLINE uint32_t Hash_String(const String* str)
    { return Hash_UTF8_Len_Caseless(String_Head(str), String_Len(str)); }

INLINE Offset First_Hash_Candidate_Slot(
    Length *skip_out,
    uint32_t hash,
    Length num_slots
){
    *skip_out = (hash & 0x0000FFFF) % num_slots;
    if (*skip_out == 0)
        *skip_out = 1;
    return (hash & 0x00FFFF00) % num_slots;
}


//=//// REBSTR COPY HELPERS ///////////////////////////////////////////////=//

#define Copy_String_At(v) \
    Copy_String_At_Limit((v), -1)

INLINE Series* Copy_Binary_At_Len(
    const Series* s,
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
INLINE Context* Error_Illegal_Cr(const Byte* at, const Byte* start)
{
    assert(*at == CR);
    REBLEN back_len = 0;
    Utf8(const*) back = cast(Utf8(const*), at);
    while (back_len < 41 and back != start) {
        back = Step_Back_Codepoint(back);
        ++back_len;
    }
    Value* str = rebSizedText(
        c_cast(char*, back),
        at - c_cast(Byte*, back) + 1  // include CR (escaped, e.g. ^M)
    );
    Context* error = Error_Illegal_Cr_Raw(str);
    rebRelease(str);
    return error;
}


// This routine is formulated in a way to try and share it in order to not
// repeat code for implementing Reb_Strmode many places.  See notes there.
//
INLINE bool Should_Skip_Ascii_Byte_May_Fail(
    const Byte* bp,
    enum Reb_Strmode strmode,
    const Byte* start  // need for knowing how far back for error context
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
    (Should_Skip_Ascii_Byte_May_Fail((bp), (strmode), (start)), NOOP)

#define Append_String(dest,string) \
    Append_String_Limit((dest), (string), UNLIMITED)
