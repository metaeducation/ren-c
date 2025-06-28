//
//  file: %stub-strand.h
//  summary: "Definitions for Strand (and the Symbol subclass of Strand)"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// In order to avoid conflict with the user-exposed notion of a "String" as
// being ANY-STRING? (e.g. a TEXT! or a TAG!, etc.) the Flex subclass for
// strings is called a "Strand".  This is more punchy than "StringBuffer"
// yet it still can have variables abbreviated as "str" or "s" in code.
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
// * Noticing when number of codepoints equals the size in bytes, and knowing
//   that means O(1) seeks are legal.
//
// * Maintaining caches (called "Bookmarks") that map from codepoint indexes
//   to byte offsets for larger strings.  These caches must be updated
//   whenever the string is modified.   !!! Only one bookmark per string ATM
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * UTF-8 String Flexes are "byte-sized", which is also true in BLOB!
//   datatypes.  However, the Flex used to store UTF-8 strings also stores
//   information about their length in codepoints in their Stubs (the main
//   "number of bytes used" in the Flex conveys bytes, not codepoints).
//
//   See the distinction between Flex_Used() and Strand_Len().
//


INLINE Length Misc_Num_Codepoints(const Strand* s) {
    assert(Is_Stub_Non_Symbol(s));
    return MISC_STRAND_NUM_CODEPOINTS(s);
}

INLINE void Tweak_Misc_Num_Codepoints(Strand* s, Length num_codepoints) {
    assert(Is_Stub_Non_Symbol(s));
    MISC_STRAND_NUM_CODEPOINTS(s) = num_codepoints;
}

INLINE Option(BookmarkList*) Link_Bookmarks(const Strand* s) {
    assert(Is_Stub_Non_Symbol(s));
    return cast(BookmarkList*, s->link.base);
}

INLINE void Tweak_Link_Bookmarks(const Strand* s, Option(BookmarkList*) book) {
    assert(Is_Stub_Non_Symbol(s));
    LINK_STRAND_BOOKMARKS(s) = maybe book;
}


//=//// STRING NAVIGATION /////////////////////////////////////////////////=//

INLINE Utf8(*) Skip_Codepoint(Utf8(const_if_c*) utf8) {
    Byte* bp = m_cast(Byte*, utf8);
    do {
        ++bp;
    } while (Is_Continuation_Byte(*bp));
    return cast(Utf8(*), bp);
}

INLINE Utf8(*) Step_Back_Codepoint(Utf8(const_if_c*) utf8) {
    Byte* bp = m_cast(Byte*, utf8);
    do {
        --bp;
    } while (Is_Continuation_Byte(*bp));
    return cast(Utf8(*), bp);
}

INLINE Utf8(*) Utf8_Next(
    Codepoint* codepoint_out,
    Utf8(const_if_c*) utf8
){
    Byte* bp = m_cast(Byte*, utf8);
    if (*bp < 0x80)
        *codepoint_out = *bp;
    else
        bp = m_cast(Byte*, Back_Scan_Utf8_Char_Unchecked(codepoint_out, bp));
    return cast(Utf8(*), bp + 1);  // see definition of Back_Scan() for why +1
}

INLINE Utf8(*) Utf8_Back(
    Codepoint* codepoint_out,
    Utf8(const_if_c*) utf8
){
    Byte* bp = m_cast(Byte*, utf8);
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
    return w_cast(Utf8(*), utf8);
}

#if CPLUSPLUS_11
    // See the definition of `const_if_c` for the explanation of why this
    // overloading technique is needed to make output constness match input.

    INLINE Utf8(const*) Skip_Codepoint(Utf8(const*) utf8)
      { return Skip_Codepoint(w_cast(Utf8(*), utf8)); }

    INLINE Utf8(const*) Step_Back_Codepoint(Utf8(const*) utf8)
      { return Step_Back_Codepoint(w_cast(Utf8(*), utf8)); }

    INLINE Utf8(const*) Utf8_Next(
        Codepoint* codepoint_out,
        Utf8(const*) utf8
    ){
        return Utf8_Next(codepoint_out, w_cast(Utf8(*), utf8));
    }

    INLINE Utf8(const*) Utf8_Back(
        Codepoint* codepoint_out,
        Utf8(const*) utf8
    ){
        return Utf8_Back(codepoint_out, w_cast(Utf8(*), utf8));
    }

    INLINE Utf8(const*) Utf8_Skip(
        Codepoint* codepoint_out,
        Utf8(const*) utf8,
        REBINT delta
    ){
        return Utf8_Skip(codepoint_out, w_cast(Utf8(*), utf8), delta);
    }
#endif

INLINE bool Codepoint_At_Is_NUL_0(Utf8(const*) utf8) {
    Byte b = u_c_cast(Byte*, utf8)[0];
    possibly(Is_Continuation_Byte(b));
    return b == '\0';
}

INLINE bool Codepoint_Back_Is_Ascii_Value(Utf8(const*) utf8, Codepoint ascii) {
    assert(ascii < 0x80);
    Byte b = u_c_cast(Byte*, utf8)[-1];
    possibly(Is_Continuation_Byte(b));
    return b == ascii;
}

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


//=//// STRING ALL-ASCII TEST /////////////////////////////////////////////=//
//
// We can test if UTF-8 strings contain only ASCII codepoints by seeing if
// their length in codepoints is equal to their size in bytes.
//
// * Symbol strings are created once and are immutable, hence they can
//   cache a flag saying whether they're all ascii or not.
//
// * Non-Symbol strings cache their length in codepoints, which can be
//   compared with the stored used size in bytes.  If these are equal
//   then the string is all ASCII.
//

INLINE bool Is_Strand_All_Ascii(const Strand* str) {
    if (Is_Stub_Symbol(str))
        return Get_Flavor_Flag(SYMBOL, str, ALL_ASCII);
    return Flex_Used(str) == MISC_STRAND_NUM_CODEPOINTS(str);
}

#define Strand_Utf8(s)      Flex_Head(char, ensure(const Strand*, s))
#define Strand_Head(s)      c_cast(Utf8(*), Flex_Head(Byte, s))
#define Strand_Tail(s)      c_cast(Utf8(*), Flex_Tail(Byte, s))

#define Strand_Size(s) \
    Flex_Used(ensure(const Strand*, s))  // encoded byte size, not codepoints

#define String_Dynamic_Size(s) \
    Flex_Dynamic_Used(ensure(const Strand*, s))

INLINE Length Strand_Len(const Strand* s) {
    if (not Is_Strand_Symbol(s)) {
      #if DEBUG_UTF8_EVERYWHERE
        if (MISC_STRAND_NUM_CODEPOINTS(s) > Flex_Used(s))  // 0xDECAFBAD counts
            crash (s);
      #endif
        return MISC_STRAND_NUM_CODEPOINTS(s);  // cache for non-ANY-WORD?
    }

    Length len = 0;  // no length cache; hope symbol is short!
    Utf8(const*) ep = Strand_Tail(s);
    Utf8(const*) cp = Strand_Head(s);
    while (cp != ep) {
        cp = Skip_Codepoint(cp);
        ++len;
    }
    return len;
}

INLINE REBLEN Strand_Index_At(
    const Strand* s,
    Size byteoffset  // offset must be at an encoded codepoint start
){
    if (Is_Strand_All_Ascii(s))
        return byteoffset;

    assert(not Is_Continuation_Byte(*Binary_At(s, byteoffset)));

    if (Is_Stub_Non_Symbol(s)) {  // length is cached for non-ANY-WORD?
      #if DEBUG_UTF8_EVERYWHERE
        if (MISC_STRAND_NUM_CODEPOINTS(s) > Flex_Used(s))  // also 0xDECAFBAD
            crash (s);
      #endif

        // We have length and bookmarks.  We should build Strand_At() based on
        // this routine.  For now, fall through and do it slowly.
    }

    // Have to do it the slow way if it's a Symbol Flex...but hopefully
    // they're not too long (since spaces and newlines are illegal.)
    //
    REBLEN index = 0;
    Utf8(const*) ep = cast(Utf8(const*), Binary_At(s, byteoffset));
    Utf8(const*) cp = Strand_Head(s);
    while (cp != ep) {
        cp = Skip_Codepoint(cp);
        ++index;
    }
    return index;
}

INLINE void Set_Strand_Len_Size(Strand* s, Length len, Size used) {
    assert(not Is_Strand_Symbol(s));
    assert(len <= used);
    assert(used == Flex_Used(s));
    MISC_STRAND_NUM_CODEPOINTS(s) = len;
    assert(*Binary_At(s, used) == '\0');
    UNUSED(used);
}

INLINE void Term_Strand_Len_Size(Strand* s, Length len, Size used) {
    assert(not Is_Strand_Symbol(s));
    assert(len <= used);
    Set_Flex_Used(s, used);
    MISC_STRAND_NUM_CODEPOINTS(s) = len;
    *Binary_At(s, used) = '\0';
}


//=//// CACHED ACCESSORS AND BOOKMARKS ////////////////////////////////////=//
//
// BookMarkList in this terminology is simply a Flex which contains a list
// of indexes and offsets.  This helps to accelerate finding positions in
// UTF-8 strings based on index, vs. having to necessarily search from the
// head or tail.
//
// !!! At the moment, only one bookmark is in effect at a time.  Even though
// it's just two numbers, there's only one pointer's worth of space in the
// Flex Stub otherwise.  Bookmarks aren't generated for a String that is
// very short, or one that is never enumerated.

#define BOOKMARK_INDEX(b) \
    Flex_Head(Bookmark, (b))->index

#define BOOKMARK_OFFSET(b) \
    Flex_Head(Bookmark, (b))->offset

INLINE BookmarkList* Alloc_BookmarkList(void) {
    BookmarkList* books = Make_Flex(
        FLAG_FLAVOR(FLAVOR_BOOKMARKLIST)
            | BASE_FLAG_MANAGED,  // lie to be untracked
        BookmarkList,
        1
    );
    Clear_Base_Managed_Bit(books);  // untracked and indefinite lifetime
    Set_Flex_Len(books, 1);
    return books;
}

INLINE void Free_Bookmarks_Maybe_Null(Strand* str) {
    assert(not Is_Strand_Symbol(str));
    BookmarkList* book = maybe Link_Bookmarks(str);
    if (book) {
        GC_Kill_Flex(book);
        Tweak_Link_Bookmarks(str, nullptr);
    }
}

#if RUNTIME_CHECKS
    INLINE void Check_Bookmarks_Debug(Strand* s) {
        BookmarkList* book = maybe Link_Bookmarks(s);
        if (not book)
            return;

        REBLEN index = BOOKMARK_INDEX(book);
        Size offset = BOOKMARK_OFFSET(book);

        Utf8(*) cp = Strand_Head(s);
        REBLEN i;
        for (i = 0; i != index; ++i)
            cp = Skip_Codepoint(cp);

        Size actual = cast(Byte*, cp) - Flex_Data(s);
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
    INLINE Utf8(const*) Strand_At(const Strand* s, REBLEN at)
      { return Strand_At(m_cast(Strand*, s), at); }
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

INLINE Codepoint Get_Strand_Char_At(const Strand* s, REBLEN n) {
    Utf8(const*) up = Strand_At(s, n);
    Codepoint c;
    Utf8_Next(&c, up);
    return c;
}


// !!! This code is a subset of what Modify_String() can also handle.  Having
// it is an optimization that may-or-may-not be worth the added complexity of
// having more than one way of doing a CHANGE to a character.  Review.
//
INLINE void Set_Char_At(Strand* s, REBLEN n, Codepoint c) {
    //
    // We are maintaining the same length, but DEBUG_UTF8_EVERYWHERE will
    // corrupt the length every time the Flex_Used() changes.  Workaround that
    // by saving the length and restoring at the end.
    //
  #if DEBUG_UTF8_EVERYWHERE
    REBLEN len = Strand_Len(s);
  #endif

    assert(not Is_Strand_Symbol(s));  // symbols are immutable
    assert(n < Strand_Len(s));

    Utf8(*) cp = Strand_At(s, n);
    Utf8(*) old_next_cp = Skip_Codepoint(cp);  // scans fast (for leading bytes)

    Size size = Encoded_Size_For_Codepoint(c);
    Size old_size = old_next_cp - cp;
    if (size == old_size) {
        // common case... no memory shuffling needed, no bookmarks need
        // to be updated.
    }
    else {
        Size cp_offset = cp - Strand_Head(s);  // for updating bookmark, expand

        int delta = size - old_size;
        if (delta < 0) {  // shuffle forward, memmove() vs memcpy(), overlaps!
            memmove(
                cast(Byte*, cp) + size,
                cast(Byte*, old_next_cp),
                Strand_Tail(s) - old_next_cp
            );

            Set_Flex_Used(s, Flex_Used(s) + delta);
        }
        else {
            Expand_Flex_Tail(s, delta);  // this adds to SERIES_USED
            cp = cast(Utf8(*),  // refresh `cp` (may've reallocated!)
                cast(Byte*, Strand_Head(s)) + cp_offset
            );
            Byte* later = cast(Byte*, cp) + delta;
            memmove(
                later,
                cast(Byte*, cp),
                cast(Byte*, Strand_Tail(s)) - later
            );  // Note: may not be terminated
        }

        *cast(Byte*, Strand_Tail(s)) = '\0';  // add terminator

        // `cp` still is the start of the character for the index we were
        // dealing with.  Only update bookmark if it's an offset *after*
        // that character position...
        //
        BookmarkList* book = maybe Link_Bookmarks(s);
        if (book and BOOKMARK_OFFSET(book) > cp_offset)
            BOOKMARK_OFFSET(book) += delta;
    }

  #if DEBUG_UTF8_EVERYWHERE  // see note on `len` at start of function
    MISC_STRAND_NUM_CODEPOINTS(s) = len;
  #endif

    Encode_UTF8_Char(cp, c, size);
    Assert_Flex_Term_If_Needed(s);
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


//=//// STRAND CREATION HELPERS ///////////////////////////////////////////=//
//
// Note that most clients should be using the rebStringXXX() APIs for this
// and generate Value*.  Note also that these routines may panic() if the
// data they are given is not UTF-8.

#define Make_Strand(encoded_capacity) \
    Make_Strand_Core(STUB_MASK_STRAND, (encoded_capacity))

INLINE Strand* Make_Strand_UTF8(const char *utf8) {
    return Append_UTF8_May_Panic(nullptr, utf8, strsize(utf8), STRMODE_NO_CR);
}

INLINE Strand* Make_Sized_Strand_UTF8(const char *utf8, size_t size) {
    return Append_UTF8_May_Panic(nullptr, utf8, size, STRMODE_NO_CR);
}


//=//// STRAND HASHING ////////////////////////////////////////////////////=//

INLINE uint32_t Hash_Strand(const Strand* str)
    { return Hash_UTF8_Len_Caseless(Strand_Head(str), Strand_Len(str)); }

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


//=//// STRAND COPY HELPERS ///////////////////////////////////////////////=//

#define Copy_String_At(v) \
    Copy_String_At_Limit((v), UNLIMITED)

INLINE Binary* Copy_Binary_At_Len(
    const Binary* b,
    REBLEN index,
    REBLEN len
){
    return cast(Binary*, Copy_Flex_At_Len_Extra(
        FLAG_FLAVOR(FLAVOR_BINARY) | FLEX_FLAGS_NONE,
        b,
        index,
        len,
        0
    ));
}


// Conveying the part of a string which contains a CR byte is helpful.  But
// we may see this CR during a scan...e.g. the bytes that come after it have
// not been checked to see if they are valid UTF-8.  We assume all the bytes
// *prior* are known to be valid.
//
INLINE Error* Error_Illegal_Cr(const Byte* at, const Byte* start)
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
    Error* error = Error_Illegal_Cr_Raw(str);
    rebRelease(str);
    return error;
}


// This routine is formulated in a way to try and share it in order to not
// repeat code for implementing Reb_Strmode many places.  See notes there.
//
INLINE bool Should_Skip_Ascii_Byte_May_Panic(
    const Byte* bp,
    enum Reb_Strmode strmode,
    const Byte* start  // need for knowing how far back for error context
){
    if (*bp == '\0')
        panic (Error_Illegal_Zero_Byte_Raw());  // never allow #{00} in strings

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
            panic (Error_Illegal_Cr(bp, start));

          case STRMODE_LF_TO_CRLF:
            assert(!"STRMODE_LF_TO_CRLF handled by exporting routines only");
            break;
        }
    }

    return false;  // character is okay for string, don't skip
}

#define Validate_Ascii_Byte(bp,strmode,start) \
    (Should_Skip_Ascii_Byte_May_Panic((bp), (strmode), (start)), NOOP)

#define Append_Any_Utf8(dest,string) \
    Append_Any_Utf8_Limit((dest), (string), UNLIMITED)
