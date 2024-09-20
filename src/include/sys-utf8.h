//
//  File: %sys-utf8.h
//  Summary: "CHAR! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Portions here are derived from the files ConvertUTF.h and ConvertUTF.c,
// by Unicode Inc.  The files are no longer available from Unicode.org but
// can be found in some other projects, including Android:
//
// https://android.googlesource.com/platform/external/id3lib/+/master/unicode.org/ConvertUTF.h
// https://android.googlesource.com/platform/external/id3lib/+/master/unicode.org/ConvertUTF.c
// https://stackoverflow.com/q/2685004/
//
//     Copyright 2001-2004 Unicode, Inc.
//
//     Disclaimer
//
//     This source code is provided as is by Unicode, Inc. No claims are
//     made as to fitness for any particular purpose. No warranties of any
//     kind are expressed or implied. The recipient agrees to determine
//     applicability of information provided. If this file has been
//     purchased on magnetic or optical media from Unicode, Inc., the
//     sole remedy for any claim will be exchange of defective media
//     within 90 days of receipt.
//
//     Limitations on Rights to Redistribute This Code
//
//     Unicode, Inc. hereby grants the right to freely use the information
//     supplied in this file in the creation of products supporting the
//     Unicode Standard, and to make copies of this file in any form
//     for internal or external distribution as long as this notice
//     remains attached.
//

//=//// MAXIMUM CODEPOINT SIZE ////////////////////////////////////////////=//
//
// Historically there is some disagremeent on UTF-8 codepoint maximum size:
//
//     "UTF-8 was originally specified to allow codepoints with up to
//     31 bits (or 6 bytes). But with RFC3629, this was reduced to 4
//     bytes max. to be more compatible to UTF-16."  So depending on
//     which RFC you consider "the UTF-8", max size is either 4 or 6.
//
// The general consensus is thus 4 bytes:
//
// https://stackoverflow.com/a/9533324
//
// BUT since Rebol is "idealistic" and not interested in UTF-16 in the long
// tail of things, we will likely want to build on what the protocol is
// abstractly capable of...thinking of "strings" as any case of numbers where
// the smaller numbers are more common than the big ones.  Then any limits
// would be part of the codecs and defaults, vs. core implementation limits.
// For the moment several places assume 4, which should be re-evaluated...
// so be sure to use this constant instead of just "4" to help find them.
//
#define UNI_ENCODED_MAX 4


#define Is_Continuation_Byte(b) \
    (((b) & 0xC0) == 0x80)  // only certain if UTF-8 validity is already known

extern const uint_fast8_t g_first_byte_mark_utf8[7];  // defined in %t-char.c
extern const char g_trailing_bytes_for_utf8[256];  // defined in %t-char.c
extern const uint_fast32_t g_offsets_from_utf8[6];  // defined in %t-char.c

#define UNI_REPLACEMENT_CHAR    (Codepoint)0x0000FFFD
#define UNI_MAX_BMP             (Codepoint)0x0000FFFF
#define UNI_MAX_UTF16           (Codepoint)0x0010FFFF
#define UNI_MAX_UTF32           (Codepoint)0x7FFFFFFF
#define UNI_MAX_LEGAL_UTF32     (Codepoint)0x0010FFFF

#define UNI_SUR_HIGH_START  (Codepoint)0xD800
#define UNI_SUR_HIGH_END    (Codepoint)0xDBFF
#define UNI_SUR_LOW_START   (Codepoint)0xDC00
#define UNI_SUR_LOW_END     (Codepoint)0xDFFF

#define MAX_UNI UNI_MAX_LEGAL_UTF32  // https://stackoverflow.com/a/20883643

// 1. Some languages have the feature of decoding illegal codepoints as a
//    substitution character.  If Rebol were willing to do this, at what level
//    would that decision be made?
//
INLINE uint_fast8_t Encoded_Size_For_Codepoint(Codepoint c) {
    if (c < cast(uint32_t, 0x80))
        return 1;
    if (c < cast(uint32_t, 0x800))
        return 2;
    if (c < cast(uint32_t, 0x10000))
        return 3;
   if (c <= UNI_MAX_LEGAL_UTF32)
        return UNI_ENCODED_MAX;

    /*len = 3;
    c = UNI_REPLACEMENT_CHAR; */  // 1. previous code could tolerate

    fail ("Codepoint is greater than maximum legal UTF-32 value");
}

// Encodes a single codepoint with known size (see Write_Codepoint() wrapper)
// Be sure dst has at least `encoded_size` bytes available.
//
INLINE void Encode_UTF8_Char(
    Byte* dst,
    Codepoint c,
    uint_fast8_t encoded_size  // must match Encoded_Size_For_Codepoint(c)
){
    const uint32_t mask = 0xBF;
    const uint32_t mark = 0x80;

    dst += encoded_size;

    switch (encoded_size) {
      case 4:
        *--dst = cast(Byte, (c | mark) & mask);
        c >>= 6;  // falls through
      case 3:
        *--dst = cast(Byte, (c | mark) & mask);
        c >>= 6;  // falls through
      case 2:
        *--dst = cast(Byte, (c | mark) & mask);
        c >>= 6;  // falls through
      case 1:
        *--dst = cast(Byte, c | g_first_byte_mark_utf8[encoded_size]);
    }
}

// Wide characters are supported by the API, due to their prevalence in
// things like Windows and ODBC.

INLINE void Encode_UTF16_Pair(Codepoint codepoint, REBWCHAR *units)
{
    uint32_t adjusted;
    assert(0x10000 <= codepoint and codepoint <= UNI_MAX_UTF16);
    adjusted = (codepoint - 0x10000);
    units[0] = UNI_SUR_HIGH_START | (adjusted >> 10);
    units[1] = UNI_SUR_LOW_START | (adjusted & 0x3FF);
}

INLINE Codepoint Decode_UTF16_Pair(const REBWCHAR *units)
{
    uint32_t adjusted;
    assert(UNI_SUR_HIGH_START <= units[0] and units[0] <= UNI_SUR_HIGH_END);
    assert(UNI_SUR_LOW_START <= units[1] and units[1] <= UNI_SUR_LOW_END);
    adjusted = 0x10000;
    adjusted += (units[0] & 0x03FF) << 10;
    adjusted += (units[1] & 0x03FF);
    return adjusted;
}

enum {
    BEL = 7,
    BS = 8,
    LF = 10,
    CR = 13,
    ESC = 27,
    DEL = 127
};

#define UNICODE_CASES 0x2E00  // size of unicode folding table

// !!! Cases present a lot of problems.  Technically speaking the upper and
// lowercase sizes of a character may not be the same:
//
// https://stackoverflow.com/q/14792841/
//
// Unicode "case folding" is more complex than this table used by R3-Alpha.

INLINE Codepoint UP_CASE(Codepoint c)
  { assert(c != '\0'); return c < UNICODE_CASES ? Upper_Cases[c] : c; }

INLINE Codepoint LO_CASE(Codepoint c)
  { assert(c != '\0'); return c < UNICODE_CASES ? Lower_Cases[c] : c; }

INLINE bool Is_Codepoint_Whitespace(Codepoint c)
  { assert(c != '\0'); return c <= 32 and ((White_Chars[c] & 1) != 0); }

INLINE bool Is_Codepoint_Space(Codepoint c)
  { assert(c != '\0'); return c <= 32 and ((White_Chars[c] & 2) != 0); }


// Utility routine to tell whether a sequence of bytes is legal UTF-8.
// This must be called with the length pre-determined by the first byte.
// If not calling this from ConvertUTF8to*, then the length can be set by:
//
//  length = g_trailing_bytes_for_utf8[*source] + 1;
//
// and the sequence is illegal right away if there aren't that many bytes
// available.
//
// If presented with a length > 4, this returns false.  The Unicode
// definition of UTF-8 goes up to 4-byte sequences.
//
INLINE bool Is_Legal_UTF8(const Byte* source, int length) {
    Byte a;
    const Byte* srcptr = source + length;

    switch (length) {
      default:
        return false;

      case 4:
        if ((a = (*--srcptr)) < 0x80 || a > 0xBF)
            return false;
        // falls through
      case 3:
        if ((a = (*--srcptr)) < 0x80 || a > 0xBF)
            return false;
        // falls through
      case 2:
        if ((a = (*--srcptr)) > 0xBF)
            return false;
        // falls through

        switch (*source) {
            // no fall-through in this inner switch
            case 0xE0: if (a < 0xA0) return false; break;
            case 0xED: if (a > 0x9F) return false; break;
            case 0xF0: if (a < 0x90) return false; break;
            case 0xF4: if (a > 0x8F) return false; break;
            default:   if (a < 0x80) return false; break;
        }

        // falls through
      case 1:
        if (*source >= 0x80 && *source < 0xC2)
            return false;
    }

    if (*source > 0xF4)
        return false;

    return true;
}


//
//  Back_Scan_UTF8_Char: C
//
// Converts a single UTF8 code-point and returns the position *at the
// the last byte of the character's data*.  (This differs from the usual
// `Scan_XXX` interface of returning the position after the scanned
// element, ready to read the next one.)
//
// The peculiar interface is useful in loops that are processing
// ordinary ASCII chars directly -as well- as UTF8 ones.  The loop can
// do a single byte pointer increment after both kinds of
// elements, avoiding the need to call any kind of `Scan_Ascii()`:
//
//     for (; size > 0; ++bp, --size) {
//         if (*bp < 0x80) {
//             // do ASCII stuff...
//         }
//         else {
//             Codepoint uni;
//             bp = Back_Scan_UTF8_Char(&uni, bp, &size);
//             // do UNICODE stuff...
//         }
//     }
//
// The third parameter is an optional size that will be decremented by
// the number of "extra" bytes the UTF8 has beyond a single byte character.
// This allows for decrement-style loops such as the above.
//
// If failure due to insufficient data or malformed bytes, then nullptr is
// returned (size is not advanced).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// 1. Note that Ren-C disallows internal zero bytes in ANY-STRING?, so that
//    a single pointer can be given to C for the data in APIs like rebText(),
//    with no length...and not have this be misleading or cause bugs.  Same
//    for getting back a single pointer from rebSpell() for the data and
//    not be missing some part of it.
//
// 2. This check was considered "too expensive" and omitted in R3-Alpha:
//
//      https://github.com/rebol/rebol-issues/issues/638
//
//    ...which meant that various illegal input patterns would be tolerated,
//    so long as they didn't cause crashes.  You would just not have the
//    input validated, and get garbage characters out.  The Ren-C philosophy
//    is that since this check only applies to non-ASCII, it is worth it to
//    do the validation.  And it only applies when scanning strings...once
//    they are loaded into String* we use Back_Scan_UTF8_Char_Unchecked().
//
INLINE const Byte* Back_Scan_UTF8_Char(
    Codepoint *out,  // the valid codepoint, no NUL or substitution chars [1]
    const Byte* bp,
    Option(Size*) size
){
    *out = 0;

    const Byte* source = bp;
    uint_fast8_t trail = g_trailing_bytes_for_utf8[*source];

    if (size) {  // Check that we have enough valid source bytes
        if (cast(uint_fast8_t, trail + 1) > *(unwrap size))
            return nullptr;
    }
    else if (trail != 0) {
        do {
            if (source[trail] < 0x80)
                return nullptr;
        } while (--trail != 0);

        trail = g_trailing_bytes_for_utf8[*source];
    }

    if (not Is_Legal_UTF8(source, trail + 1))  // was omitted in R3-Alpha [2]
        return nullptr;

    switch (trail) {
        case 5: *out += *source++; *out <<= 6;  // falls through
        case 4: *out += *source++; *out <<= 6;  // falls through
        case 3: *out += *source++; *out <<= 6;  // falls through
        case 2: *out += *source++; *out <<= 6;  // falls through
        case 1: *out += *source++; *out <<= 6;  // falls through
        case 0: *out += *source++;
    }
    *out -= g_offsets_from_utf8[trail];

    if (*out > UNI_MAX_LEGAL_UTF32)
        return nullptr;  // anything over Plane 17 (> 0x10FFFF) is illegal
    if (*out >= UNI_SUR_HIGH_START && *out <= UNI_SUR_LOW_END)
        return nullptr;  // UTF-16 surrogate values illegal in UTF-8

    if (size)
        *(unwrap size) -= trail;

    if (*out == 0)  // string types disallow internal 0 bytes in Ren-C [1]
        return nullptr;

    return bp + trail;
}


// This is the fast version of scanning a UTF-8 character where you assume it
// is valid UTF-8...it seeks ahead until it finds a non-continuation byte.
// Since it seeks ahead, it still has to follow the Back_Scan_UTF8_Char()
// strategy that splits ASCII codes to basic incrementation...otherwise it
// would try to read continuation bytes past a `\0` string terminator.  :-/
//
INLINE const Byte* Back_Scan_UTF8_Char_Unchecked(
    Codepoint *out,
    const Byte* bp
){
    *out = *bp;  // wait to increment...
    uint_fast8_t trail = 0;  // count as we go

    while (Is_Continuation_Byte(bp[1])) {
        *out <<= 6;
        ++bp;  // ...NOW we increment
        *out += *bp;
        ++trail;
    }
    assert(trail <= 5);

    *out -= g_offsets_from_utf8[trail];  // subtract the "magic number"

    assert(*out <= UNI_MAX_LEGAL_UTF32);
    assert(*out < UNI_SUR_HIGH_START or *out > UNI_SUR_LOW_END);

    return bp;
}
