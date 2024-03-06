//
//  File: %sys-string.h
//  Summary: {Definitions for Symbol (e.g. WORD!) and REBUNI (e.g. STRING!)}
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
// !!! THIS IS AN ANTIQUATED IMPLEMENTATION OF REBOL'S ANY-STRING! TYPE !!!
//
// This implementation was captured at a transitional point in the quest to
// implement "UTF-8 Everywhere".  That was accomplished in March 2019:
//
//   https://forum.rebol.info/t/374
//
// The complexity of UTF-8 Everywhere is such that this service branch will
// not be updated to it.  But the names have been changed to reflect what it
// does, to hopefully avoid confusion.
//
// This uses simple UCS-2 encoding for all strings--no variation in encoding.
// Symbols (e.g. those stored for WORD!) are encoded as UTF-8.
//

//=////////////////////////////////////////////////////////////////////////=//
//
//  Symbol Series (UTF-8 encoding)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The concept is that a SYM_XXX refers to one of the built-in words and can
// be used in C switch statements.  A canon Symbol is used to identify
// everything else.
//

INLINE const char *Symbol_Head(Symbol* str) {
    return cs_cast(Binary_Head(str));
}

INLINE Symbol* Canon_Symbol(Symbol* str) {
    while (NOT_SER_INFO(str, STRING_INFO_CANON))
        str = LINK(str).synonym; // circularly linked list
    return str;
}

INLINE Option(SymId) Symbol_Id(Symbol* str) {
    uint16_t sym = SECOND_UINT16(&str->header);
    assert(sym == SECOND_UINT16(&Canon_Symbol(str)->header));
    return cast(SymId, sym);
}

INLINE size_t Symbol_Size(Symbol* str) {
    return Series_Len(str); // number of bytes in seris is series length, ATM
}

INLINE Symbol* Canon(SymId sym) {
    assert(cast(REBLEN, sym) != 0);
    assert(cast(REBLEN, sym) < Series_Len(PG_Symbol_Canons));
    return *Series_At(Symbol*, PG_Symbol_Canons, cast(REBLEN, sym));
}

INLINE bool Are_Synonyms(Symbol* s1, Symbol* s2) {
    if (s1 == s2)
        return true; // !!! does this check speed things up or not?
    return Canon_Symbol(s1) == Canon_Symbol(s2); // canon check, quite fast
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  UCS-2 series for ANY-STRING!
//
//=////////////////////////////////////////////////////////////////////////=//
//

INLINE bool Is_Series_Ucs2(Series* s) {
    //
    // There's no specific flag for UCS-2, but these are the only 2-byte
    // series at the moment.
    //
    return Series_Wide(s) == sizeof(REBUNI);
}

INLINE REBLEN String_Len(String* s) {
    assert(Series_Wide(s) == sizeof(REBUNI));
    return Series_Len(s);
}

INLINE void Set_String_Len(String* s, REBLEN len) {
    assert(Series_Wide(s) == sizeof(REBUNI));
    Set_Series_Len(s, len);
}

#define String_At(s,n) \
    AS_REBCHR(Series_At(REBUNI, (s), (n)))

#define String_Head(s) \
    Series_Head(REBUNI, (s))

#define String_Tail(s) \
    Series_Tail(REBUNI, (s))

#define String_Last(s) \
    Series_Last(REBUNI, (s))

INLINE void Term_String_Len(String* s, REBLEN len) {
    Set_Series_Len(s, len);
    *Series_At(REBUNI, s, len) = '\0';
}

INLINE String* Cell_String(const Cell* cell) {
    assert(ANY_STRING(cell));
    return cast(String*, VAL_SERIES(cell));
}

#define Cell_String_Head(v) \
    String_Head(Cell_String(v))

#define Cell_String_Tail(v) \
    String_Tail(Cell_String(v))

INLINE REBUNI *Cell_String_At(const Cell* v) {
    return AS_REBUNI(String_At(Cell_String(v), VAL_INDEX(v)));
}

INLINE REBSIZ VAL_SIZE_LIMIT_AT(
    REBLEN *length, // length in chars to end (including limit)
    const Cell* v,
    REBINT limit // -1 for no limit
){
    assert(ANY_STRING(v));

    Ucs2(const*) at = Cell_String_At(v); // !!! update cache if needed
    Ucs2(const*) tail;

    if (limit == -1) {
        if (length != nullptr)
            *length = VAL_LEN_AT(v);
        tail = Cell_String_Tail(v); // byte count known (fast)
    }
    else {
        if (length != nullptr)
            *length = limit;
        tail = at;
        for (; limit > 0; --limit)
            tail = Ucs2_Next(nullptr, tail);
    }

    return (
        cast(const Byte*, AS_REBUNI(tail))
        - cast(const Byte*, AS_REBUNI(at))
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

INLINE REBUNI GET_ANY_CHAR(Series* s, REBLEN n) {
    return BYTE_SIZE(s) ? *Binary_At(s, n) : *Series_At(REBUNI, s, n);
}

INLINE void SET_ANY_CHAR(Series* s, REBLEN n, REBUNI c) {
    if (BYTE_SIZE(s)) {
        assert(c <= 255);
        *Binary_At(s, n) = c;
    }
    else
        *Series_At(REBUNI, s, n) = c;
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
INLINE const Byte *Back_Scan_UTF8_Char(
    REBUNI *out,
    const Byte *bp,
    REBSIZ *size
){
    unsigned long ch; // "UTF32" is defined as unsigned long
    const Byte *bp_new = Back_Scan_UTF8_Char_Core(&ch, bp, size);
    if (bp_new and ch > 0xFFFF)
        fail (Error_Codepoint_Too_High_Raw(rebInteger(ch)));
    *out = cast(REBUNI, ch);
    return bp_new;
}


// Basic string initialization from UTF8.  (Most clients should be using the
// rebStringXXX() APIs for this).  Note that these routines may fail() if the
// data they are given is not UTF-8.

INLINE String* Make_String_UTF8(const char *utf8)
{
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Fail(nullptr, utf8, strsize(utf8), crlf_to_lf);
}

INLINE String* Make_Sized_String_UTF8(const char *utf8, size_t size)
{
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Fail(nullptr, utf8, size, crlf_to_lf);
}


INLINE REBINT Hash_String(Symbol* str)
    { return Hash_UTF8(cb_cast(Symbol_Head(str)), Symbol_Size(str)); }

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

INLINE Series* Copy_Sequence_At_Position(const Value* v)
{
    return Copy_Sequence_At_Len_Extra(
        VAL_SERIES(v), VAL_INDEX(v), VAL_LEN_AT(v), 0
    );
}

INLINE Series* Copy_Sequence_At_Len(
    Series* s,
    REBLEN index,
    REBLEN len
){
    return Copy_Sequence_At_Len_Extra(s, index, len, 0);
}
