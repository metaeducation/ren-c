//
//  File: %sys-string.h
//  Summary: {Definitions for REBSTR (e.g. WORD!) and REBUNI (e.g. STRING!)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! R3-Alpha and Red would work with strings in their decoded form, in
// series of varying widths.  Ren-C's goal is to replace this with the idea
// of "UTF-8 everywhere", working with the strings as UTF-8 and only
// converting if the platform requires it for I/O (e.g. Windows):
//
// http://utf8everywhere.org/
//
// As a first step toward this goal, one place where strings were kept in
// UTF-8 form has been converted into series...the word table.  So for now,
// all REBSTR instances are for ANY-WORD!.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The *current* implementation of Rebol's ANY-STRING! type has two different
// series widths that are used.  One is the BYTE_SIZED() series which encodes
// ASCII in the low bits, and Latin-1 extensions in the range 0x80 - 0xFF.
// So long as a codepoint can fit in this range, the string can be stored in
// single bytes:
//
// https://en.wikipedia.org/wiki/Latin-1_Supplement_(Unicode_block)
//
// (Note: This is not to be confused with the other "byte-width" encoding,
// which is UTF-8.  Rebol series routines are not set up to handle insertions
// or manipulations of UTF-8 encoded data in a Reb_Any_String payload at
// this time...it is a format used only in I/O.)
//
// The second format that is used puts codepoints into a 16-bit REBUNI-sized
// element.  If an insertion of a string or character into a byte sized
// string cannot be represented in 0xFF or lower, then the target string will
// be "widened"--doubling the storage space taken and requiring updating of
// the character data in memory.  At this time there are no "in-place"
// cases where a string is reduced from REBUNI to byte sized, but operations
// like Copy_String_At_Len() will scan a source string to see if a byte-size
// copy can be made from a REBUNI-sized one without loss of information.
//
// Byte-sized series are also used by the BINARY! datatype.  There is no
// technical difference between such series used as strings or used as binary,
// the difference comes from being marked REB_BINARY or REB_TEXT in the
// header of the value carrying the series.
//
// For easier type-correctness, the series macros are given with names BIN_XXX
// and UNI_XXX.  There aren't distinct data types for the series themselves,
// just REBSER* is used.  Hence BIN_LEN() and UNI_LEN() aren't needed as you
// could just use SER_LEN(), but it helps a bit for readability...and an
// assert is included to ensure the size matches up.
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBSTR series for UTF-8 strings
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The concept is that a SYM refers to one of the built-in words and can
// be used in C switch statements.  A canon STR is used to identify
// everything else.
//

INLINE const char *STR_HEAD(REBSTR *str) {
    return cs_cast(BIN_HEAD(str));
}

INLINE REBSTR *STR_CANON(REBSTR *str) {
    while (NOT_SER_INFO(str, STRING_INFO_CANON))
        str = LINK(str).synonym; // circularly linked list
    return str;
}

INLINE Option(SymId) STR_SYMBOL(REBSTR *str) {
    uint16_t sym = SECOND_UINT16(str->header);
    assert(sym == SECOND_UINT16(STR_CANON(str)->header));
    return cast(SymId, sym);
}

INLINE size_t STR_SIZE(REBSTR *str) {
    return SER_LEN(str); // number of bytes in seris is series length, ATM
}

INLINE REBSTR *Canon(SymId sym) {
    assert(cast(REBLEN, sym) != 0);
    assert(cast(REBLEN, sym) < SER_LEN(PG_Symbol_Canons));
    return *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBLEN, sym));
}

INLINE bool SAME_STR(REBSTR *s1, REBSTR *s2) {
    if (s1 == s2)
        return true; // !!! does this check speed things up or not?
    return STR_CANON(s1) == STR_CANON(s2); // canon check, quite fast
}



//
// !!! UNI_XXX: Unicode string series macros !!! - Becoming Deprecated
//

INLINE REBLEN UNI_LEN(REBSER *s) {
    assert(SER_WIDE(s) == sizeof(REBUNI));
    return SER_LEN(s);
}

INLINE void SET_UNI_LEN(REBSER *s, REBLEN len) {
    assert(SER_WIDE(s) == sizeof(REBUNI));
    SET_SERIES_LEN(s, len);
}

#define UNI_AT(s,n) \
    AS_REBCHR(SER_AT(REBUNI, (s), (n)))

#define UNI_HEAD(s) \
    SER_HEAD(REBUNI, (s))

#define UNI_TAIL(s) \
    SER_TAIL(REBUNI, (s))

#define UNI_LAST(s) \
    SER_LAST(REBUNI, (s))

INLINE void TERM_UNI_LEN(REBSER *s, REBLEN len) {
    SET_SERIES_LEN(s, len);
    *SER_AT(REBUNI, s, len) = '\0';
}

#define VAL_UNI_HEAD(v) \
    UNI_HEAD(VAL_SERIES(v))

#define VAL_UNI_TAIL(v) \
    UNI_TAIL(VAL_SERIES(v))

// This should be an updating operation, which may refresh the cache in the
// value.  It would look something like:
//
//     if (s->stamp == v->extra.utfcache.stamp)
//          return v->extra.utfcache.offset;
//     ...else calculate...
//    m_cast(Value*, v)->extra.utfcache.stamp = s->stamp;
//    m_cast(Value*, v)->extra.utfcache.offset = offset;
//
// One should thus always prefer to use VAL_UNI_AT() if possible, over trying
// to calculate a position from scratch.
//
INLINE REBUNI *VAL_UNI_AT(const Cell* v) {
    return AS_REBUNI(UNI_AT(VAL_SERIES(v), VAL_INDEX(v)));
}

INLINE REBSIZ VAL_SIZE_LIMIT_AT(
    REBLEN *length, // length in chars to end (including limit)
    const Cell* v,
    REBINT limit // -1 for no limit
){
    assert(ANY_STRING(v));

    REBCHR(const*) at = VAL_UNI_AT(v); // !!! update cache if needed
    REBCHR(const*) tail;

    if (limit == -1) {
        if (length != nullptr)
            *length = VAL_LEN_AT(v);
        tail = VAL_UNI_TAIL(v); // byte count known (fast)
    }
    else {
        if (length != nullptr)
            *length = limit;
        tail = at;
        for (; limit > 0; --limit)
            tail = NEXT_CHR(nullptr, tail);
    }

    return (
        cast(const REBYTE*, AS_REBUNI(tail))
        - cast(const REBYTE*, AS_REBUNI(at))
    );
}


//
// Get or set a unit in a binary series or a string series.  Used by routines
// that do searching/etc. and want to apply to both BINARY! and ANY-STRING!,
// so it can't be converted to purely UTF-8 as written.
//
// !!! String logic will get more complex with UTF8-Everywhere; it may have to
// shift bytes out of the way.  Or it may not even be possible to set a
// character if there aren't characters established before it.  Any
// algorithm using these should likely instead be using the mold buffer to
// create new strings, if possible.
//

INLINE REBUNI GET_ANY_CHAR(REBSER *s, REBLEN n) {
    return BYTE_SIZE(s) ? *BIN_AT(s, n) : *SER_AT(REBUNI, s, n);
}

INLINE void SET_ANY_CHAR(REBSER *s, REBLEN n, REBUNI c) {
    if (BYTE_SIZE(s)) {
        assert(c <= 255);
        *BIN_AT(s, n) = c;
    }
    else
        *SER_AT(REBUNI, s, n) = c;
}

#define VAL_ANY_CHAR(v) \
    GET_ANY_CHAR(VAL_SERIES(v), VAL_INDEX(v))


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-STRING! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define Init_Text(v,s) \
    Init_Any_Series((v), REB_TEXT, (s))

#define Init_File(v,s) \
    Init_Any_Series((v), REB_FILE, (s))

#define Init_Email(v,s) \
    Init_Any_Series((v), REB_EMAIL, (s))

#define Init_Tag(v,s) \
    Init_Any_Series((v), REB_TAG, (s))

#define Init_Url(v,s) \
    Init_Any_Series((v), REB_URL, (s))


// R3-Alpha did not support unicode codepoints higher than 0xFFFF, because
// strings were only 1 or 2 bytes per character.  Until support for "astral
// plane" characters is added, this inline function traps large characters
// when strings are being scanned.  If a client wishes to handle them
// explicitly, use Back_Scan_UTF8_Char_Core().
//
// Though the machinery can decode a UTF32 32-bit codepoint, the interface
// uses a 16-bit REBUNI (due to that being all that Rebol supports at this
// time).  If a codepoint that won't fit in 16-bits is found, it will raise
// an error vs. return nullptr.  This makes it clear that the problem is not
// with the data itself being malformed (the usual assumption of callers)
// but rather a limit of the implementation.
//
INLINE const REBYTE *Back_Scan_UTF8_Char(
    REBUNI *out,
    const REBYTE *bp,
    REBSIZ *size
){
    unsigned long ch; // "UTF32" is defined as unsigned long
    const REBYTE *bp_new = Back_Scan_UTF8_Char_Core(&ch, bp, size);
    if (bp_new and ch > 0xFFFF)
        fail (Error_Codepoint_Too_High_Raw(rebInteger(ch)));
    *out = cast(REBUNI, ch);
    return bp_new;
}


// Basic string initialization from UTF8.  (Most clients should be using the
// rebStringXXX() APIs for this).  Note that these routines may fail() if the
// data they are given is not UTF-8.

INLINE REBSER *Make_String_UTF8(const char *utf8)
{
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Fail(nullptr, utf8, strsize(utf8), crlf_to_lf);
}

INLINE REBSER *Make_Sized_String_UTF8(const char *utf8, size_t size)
{
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Fail(nullptr, utf8, size, crlf_to_lf);
}


INLINE REBINT Hash_String(REBSTR *str)
    { return Hash_UTF8(cb_cast(STR_HEAD(str)), STR_SIZE(str)); }

INLINE REBINT First_Hash_Candidate_Slot(
    REBLEN *skip_out,
    REBLEN hash,
    REBLEN num_slots
){
    *skip_out = (hash & 0x0000FFFF) % num_slots;
    if (*skip_out == 0)
        *skip_out = 1;
    return (hash & 0x00FFFF00) % num_slots;
}


//
// Copy helpers
//

INLINE REBSER *Copy_Sequence_At_Position(const Value* v)
{
    return Copy_Sequence_At_Len_Extra(
        VAL_SERIES(v), VAL_INDEX(v), VAL_LEN_AT(v), 0
    );
}

INLINE REBSER *Copy_Sequence_At_Len(
    REBSER *s,
    REBLEN index,
    REBLEN len
){
    return Copy_Sequence_At_Len_Extra(s, index, len, 0);
}


// This is a speculative routine, which is based on the idea that it will be
// common for UTF-8 anywhere strings to cache a bit saying whether they are
// in ASCII range and fixed size.  If this is the case, different algorithms
// might be applied, for instance a standard C qsort() to sort the characters.
//
INLINE bool Is_String_ASCII(const Cell* str) {
    UNUSED(str);
    return false; // currently all strings are 16-bit REBUNI characters
}
