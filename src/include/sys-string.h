//
//  file: %sys-string.h
//  summary:{Definitions for Symbol (e.g. WORD!) and Ucs2Unit (e.g. STRING!)}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    while (Not_Flex_Info(str, CANON_SYMBOL))
        str = LINK(str).synonym; // circularly linked list
    return str;
}

INLINE Option(SymId) Symbol_Id(Symbol* str) {
    uint16_t sym = SECOND_UINT16(&str->leader);
    assert(sym == SECOND_UINT16(&Canon_Symbol(str)->leader));
    return cast(SymId, sym);
}

INLINE size_t Symbol_Size(Symbol* str) {
    return Flex_Len(str); // number of bytes in seris is series length, ATM
}

INLINE Symbol* Canon_From_Id(SymId sym) {
    assert(cast(REBLEN, sym) != 0);
    assert(cast(REBLEN, sym) < Flex_Len(PG_Symbol_Canons));
    return *Flex_At(Symbol*, PG_Symbol_Canons, cast(REBLEN, sym));
}

#define CANON(name)  Canon_From_Id(SYM_##name)

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

INLINE bool Is_Flex_Ucs2(Flex* s) {
    //
    // There's no specific flag for UCS-2, but these are the only 2-byte
    // series at the moment.
    //
    return Flex_Wide(s) == sizeof(Ucs2Unit);
}

INLINE REBLEN String_Len(Strand* s) {
    assert(Flex_Wide(s) == sizeof(Ucs2Unit));
    return Flex_Len(s);
}

INLINE void Set_String_Len(Strand* s, REBLEN len) {
    assert(Flex_Wide(s) == sizeof(Ucs2Unit));
    Set_Flex_Len(s, len);
}

INLINE Ucs2(*) Strand_At(Strand* s, REBLEN n)
  { return Flex_At(Ucs2Unit, (s), (n)); }

INLINE Ucs2Unit* Strand_Head(Strand* s)
  { return Flex_Head(Ucs2Unit, s); }

INLINE Ucs2Unit* Strand_Tail(Strand* s)
  { return Flex_Tail(Ucs2Unit, s); }

INLINE Ucs2Unit* Strand_Last(Strand* s)
  { return Series_Last(Ucs2Unit, s); }

INLINE void Term_Strand_Len(Strand* s, REBLEN len) {
    Set_Flex_Len(s, len);
    *Flex_At(Ucs2Unit, s, len) = '\0';
}

INLINE Strand* Cell_Strand(const Cell* cell) {
    assert(Any_String(cell));
    return cast(Strand*, Cell_Flex(cell));
}

#define Cell_String_Head(v) \
    Strand_Head(Cell_Strand(v))

#define Cell_String_Tail(v) \
    Strand_Tail(Cell_Strand(v))

INLINE Ucs2(*) Cell_String_At(const Cell* v) {
    return Strand_At(Cell_Strand(v), VAL_INDEX(v));
}

INLINE Size VAL_SIZE_LIMIT_AT(
    REBLEN *length, // length in chars to end (including limit)
    const Cell* v,
    REBINT limit // -1 for no limit
){
    assert(Any_String(v));

    Ucs2(const*) at = Cell_String_At(v); // !!! update cache if needed
    Ucs2(const*) tail;

    if (limit == -1) {
        if (length != nullptr)
            *length = Series_Len_At(v);
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
        cast(const Byte*, tail)
        - cast(const Byte*, at)
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

INLINE Ucs2Unit GET_ANY_CHAR(Flex* s, REBLEN n) {
    return BYTE_SIZE(s) ? *Flex_At(Byte, s, n) : *Flex_At(Ucs2Unit, s, n);
}

INLINE void SET_ANY_CHAR(Flex* s, REBLEN n, Ucs2Unit c) {
    if (BYTE_SIZE(s)) {
        assert(c <= 255);
        *Flex_At(Byte, s, n) = c;
    }
    else
        *Flex_At(Ucs2Unit, s, n) = c;
}

#define VAL_ANY_CHAR(v) \
    GET_ANY_CHAR(Cell_Flex(v), VAL_INDEX(v))


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-STRING! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define Init_Text(v,s) \
    Init_Any_Series((v), TYPE_TEXT, (s))

#define Init_File(v,s) \
    Init_Any_Series((v), TYPE_FILE, (s))

#define Init_Email(v,s) \
    Init_Any_Series((v), TYPE_EMAIL, (s))

#define Init_Tag(v,s) \
    Init_Any_Series((v), TYPE_TAG, (s))

#define Init_Url(v,s) \
    Init_Any_Series((v), TYPE_URL, (s))


// R3-Alpha did not support unicode codepoints higher than 0xFFFF, because
// strings were only 1 or 2 bytes per character.  Until support for "astral
// plane" characters is added, this inline function traps large characters
// when strings are being scanned.  If a client wishes to handle them
// explicitly, use Back_Scan_UTF8_Char_Core().
//
// Though the machinery can decode a UTF32 32-bit codepoint, the interface
// uses a 16-bit Ucs2Unit (due to that being all that Rebol supports at this
// time).  If a codepoint that won't fit in 16-bits is found, it will raise
// an error vs. return nullptr.  This makes it clear that the problem is not
// with the data itself being malformed (the usual assumption of callers)
// but rather a limit of the implementation.
//
INLINE const Byte *Back_Scan_UTF8_Char(
    Ucs2Unit* out,
    const Byte *bp,
    Size *size
){
    unsigned long ch; // "UTF32" is defined as unsigned long
    const Byte *bp_new = Back_Scan_UTF8_Char_Core(&ch, bp, size);
    if (bp_new and ch > 0xFFFF)
        panic (Error_Codepoint_Too_High_Raw(rebInteger(ch)));
    *out = cast(Ucs2Unit, ch);
    return bp_new;
}


// Basic string initialization from UTF8.  (Most clients should be using the
// rebStringXXX() APIs for this).  Note that these routines may panic() if the
// data they are given is not UTF-8.

INLINE Strand* Make_String_UTF8(const char *utf8)
{
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Panic(nullptr, utf8, strsize(utf8), crlf_to_lf);
}

INLINE Strand* Make_Sized_String_UTF8(const char *utf8, size_t size)
{
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Panic(nullptr, utf8, size, crlf_to_lf);
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

INLINE Flex* Copy_Sequence_At_Position(const Value* v)
{
    return Copy_Non_Array_Flex_At_Len_Extra(
        Cell_Flex(v), VAL_INDEX(v), Series_Len_At(v), 0
    );
}

INLINE Flex* Copy_Sequence_At_Len(
    Flex* s,
    REBLEN index,
    REBLEN len
){
    return Copy_Non_Array_Flex_At_Len_Extra(s, index, len, 0);
}
