//
//  File: %sys-token.h
//  Summary: "Definitions for an Immutable Sequence of 0 to N Codepoints"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// ISSUE! (to be renamed TOKEN!) merges historical Rebol's CHAR! and ISSUE!.
// If possible, it will store encoded UTF-8 data entirely in a cell...saving
// on allocations and improving locality.  In this system, a "character" is
// simply a sigle-length token, which is translated to a codepoint using the
// `CODEPOINT OF` reflector, or by using FIRST on the token.
//
// REB_ISSUE presents as its own datatype, but the `heart` byte in the header
// may be either REB_BYTES or REB_TEXT.  The REB_BYTES form uses the space
// that would ordinarily hold a VAL_INDEX() integer and a VAL_STRING() pointer
// for the actual encoded UTF-8 data.  Hence generically speaking, ISSUE! is
// not considered an ANY-SERIES! or ANY-STRING! type.
//
// However, there are UTF-8-based accessors VAL_UTF8_XXX which can be used to
// polymorphically access const data across ANY-STRING!, ANY-WORD!, and ISSUE!
//

inline static bool IS_CHAR_CELL(REBCEL(const*) v) {
    if (CELL_TYPE(v) != REB_ISSUE)
        return false;

    if (CELL_KIND(v) != REB_BYTES)
        return false;  // allocated form, too long to be a character

    return EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN] <= 1;  // codepoint
}

#define IS_CHAR IS_CHAR_CELL  // could be faster if used VAL_TYPE()

inline static REBUNI VAL_CHAR(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_BYTES);

    if (EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN] == 0)
        return 0;  // no '\0` bytes internal to series w/REB_TEXT "heart"

    assert(EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN] == 1);  // e.g. codepoint

    REBUNI c;
    Back_Scan_UTF8_Char_Unchecked(&c, PAYLOAD(Bytes, v).at_least_8);
    return c;
}

// !!! There used to be a cached size for the codepoint in the binary data,
// but with the "ISSUECHAR!" unification, wasting a byte for that on all forms
// seems like a bad idea for something so cheap to calculate.  But keep a
// separate entry point in case that cache comes back.
//
inline static REBYTE VAL_CHAR_ENCODED_SIZE(REBCEL(const*) v)
  { return Encoded_Size_For_Codepoint(VAL_CHAR(v)); }

inline static const REBYTE *VAL_CHAR_ENCODED(REBCEL(const*) v) {
    assert(CELL_TYPE(v) == REB_ISSUE and CELL_KIND(v) == REB_BYTES);
    assert(EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN] <= 1);  // e.g. codepoint
    return PAYLOAD(Bytes, v).at_least_8;  // !!! '\0' terminated or not?
}

inline static REBVAL *Init_Issue_Utf8(
    RELVAL *out,
    REBCHR(const*) utf8,  // previously validated UTF-8 (maybe not null term?)
    REBSIZ size,
    REBLEN len  // while validating, you should have counted the codepoints
){
    if (size + 1 <= sizeof(PAYLOAD(Bytes, out)).at_least_8) {
        RESET_CELL(out, REB_BYTES, CELL_MASK_NONE);  // no FIRST_IS_NODE
        memcpy(PAYLOAD(Bytes, out).at_least_8, utf8, size);
        PAYLOAD(Bytes, out).at_least_8[size] = '\0';
        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_USED] = size;
        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_LEN] = len;
    }
    else {
        REBSTR *str = Make_Sized_String_UTF8(cs_cast(utf8), size);
        assert(STR_LEN(str) == len);  // ^-- revalidates :-/ should match
        Freeze_Series(SER(str));
        Init_Text(out, str);
    }
    mutable_KIND_BYTE(out) = REB_ISSUE;
    return SPECIFIC(out);
}

// If you know that a codepoint is good (e.g. it came from an ANY-STRING!)
// this routine can be used.
//
inline static REBVAL *Init_Char_Unchecked(RELVAL *out, REBUNI c) {
    RESET_CELL(out, REB_BYTES, CELL_MASK_NONE);

    if (c == 0) {
        //
        // The zero codepoint is handled specially, as the empty ISSUE!.
        // This is because the system as a whole doesn't permit 0 codepoints
        // in TEXT!.  The state is recognized specially by CODEPOINT OF, but
        // still needs to be '\0' terminated (e.g. for AS TEXT!)
        //
        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_USED] = 0;
        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_LEN] = 0;
        PAYLOAD(Bytes, out).at_least_8[0] = '\0';  // terminate
    }
    else {
        REBSIZ encoded_size = Encoded_Size_For_Codepoint(c);
        Encode_UTF8_Char(PAYLOAD(Bytes, out).at_least_8, c, encoded_size);
        PAYLOAD(Bytes, out).at_least_8[encoded_size] = '\0';  // terminate

        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_USED] = encoded_size;  // bytes
        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_LEN] = 1;  // just one codepoint
    }

    mutable_KIND_BYTE(out) = REB_ISSUE;  // heart is TEXT, presents as issue
    assert(VAL_CHAR(out) == c);
    return cast(REBVAL*, out);
}

inline static REBVAL *Init_Char_May_Fail(RELVAL *out, REBUNI c) {
    if (c > MAX_UNI) {
        DECLARE_LOCAL (temp);
        fail (Error_Codepoint_Too_High_Raw(Init_Integer(temp, c)));
    }

    // !!! Should other values that can't be read be forbidden?  Byte order
    // mark?  UTF-16 surrogate stuff?  If something is not legitimate in a
    // UTF-8 codepoint stream, it shouldn't be used.

    return Init_Char_Unchecked(out, c);
}


//=//// "BLACKHOLE" (Empty ISSUE!, a.k.a. CODEPOINT 0) ////////////////////=//
//
// Validated string data is not supposed to contain zero bytes.  This means
// APIs that return only a `char*`--like rebSpell()--can assure the only `\0`
// in the data is the terminator.  BINARY! should be used for data with
// embedded bytes.  There, the extractors--like rebBytes()--require asking for
// the byte count as well as the data pointer.
//
// Since ISSUE! builds on the `heart` of a TEXT! implementation, it inherits
// the inability to store zeros in its content.  But single-codepoint tokens
// are supposed to be the replacement for CHAR!...which historically has been
// able to hold a `0` codepoint.
//
// The solution to this is to declare `codepoint of #` to be 0.  So empty
// tokens have the behavior of being appended to BINARY! and getting #{00}.
// But attempting to append them to strings will cause an error, as opposed
// to acting as a no-op.
//
// This gives `#` some attractive properties...as an "ornery-but-truthy" value
// with a brief notation.  Because '\0' codepoints don't come up that often
// in usermode code, they have another purpose which is called a "black hole".
//
// Black holes were first used to support a scenario in the multiple-return
// value code.  They indicate you want to opt-IN to a calculation, but opt-OUT
// of the result.  This is in contrast with BLANK!, which typically opts out
// of both...and the truthy nature of ISSUE! helps write clean and mostly safe
// code for it:
//
//     do-something [
//         in
//         /out [blank! word! path! blackhole!]
//         <local> result
//      ][
//          process in
//          if bar [  ; unlike BLANK!, blackhole is truthy so branch runs
//             result: process/more in
//             set out result  ; blackhole SET is no-op (BLANK! would error)
//          ]
//     ]
//
// The alias "BLACKHOLE!" is a type constraint which is today just a synonym
// for ISSUE!, but will hopefully have teeth in the future to enforce that
// it is also length 0.
//

inline static bool Is_Blackhole(const RELVAL *v) {
    if (not IS_CHAR(v))
        return false;

    if (VAL_CHAR(v) == 0)
        return true;

    // Anything that accepts "blackholes" should not have broader meaning for
    // ISSUE!s taken.  Ultimately this will be corrected for by having
    // BLACKHOLE! be a type constraint with teeth, that doesn't pass through
    // all ISSUE!s.  But for now, simplify callsites by handling the error
    // raising for them when they do the blackhole test.
    //
    fail ("Only plain # can be used with 'blackhole' ISSUE! interpretation");
}


//=//// GENERIC UTF-8 ACCESSORS //////////////////////////////////////////=//

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
    if (limit == UNLIMITED || limit > VAL_LEN_AT(v))
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
    VAL_BYTES_LIMIT_AT((size_out), (v), UNLIMITED)


// Analogous to VAL_BYTES_AT, some routines were willing to accept either an
// ANY-WORD! or an ANY-STRING! to get UTF-8 data.  This is a convenience
// routine for handling that.
//
inline static REBCHR(const*) VAL_UTF8_LEN_SIZE_AT_LIMIT(
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

    if (CELL_KIND(v) == REB_BYTES) {
        assert(CELL_TYPE(v) == REB_ISSUE);
        REBLEN len;
        REBSIZ size;
        if (limit >= EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN]) {
            len = EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN];
            size = EXTRA(Bytes, v).exactly_4[IDX_EXTRA_USED];
        }
        else {
            len = 0;
            REBCHR(const*) at = PAYLOAD(Bytes, v).at_least_8;
            for (; limit != 0; --limit, ++len)
                at = NEXT_STR(at);
            size = at - PAYLOAD(Bytes, v).at_least_8;
        }

        if (length_out)
            *length_out = len;
        if (size_out)
            *size_out = size;
        return PAYLOAD(Bytes, v).at_least_8;
    }

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
            if (limit == UNLIMITED and not length_out)
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

#define VAL_UTF8_LEN_SIZE_AT(length_out,size_out,v) \
    VAL_UTF8_LEN_SIZE_AT_LIMIT((length_out), (size_out), (v), UNLIMITED)

#define VAL_UTF8_SIZE_AT(size_out,v) \
    VAL_UTF8_LEN_SIZE_AT_LIMIT(nullptr, (size_out), (v), UNLIMITED)

#define VAL_UTF8_AT(v) \
    VAL_UTF8_LEN_SIZE_AT_LIMIT(nullptr, nullptr, (v), UNLIMITED)
