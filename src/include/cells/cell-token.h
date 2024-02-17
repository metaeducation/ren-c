//
//  File: %cell-token.h
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
// REB_ISSUE has two forms: one with a separate node allocation and one that
// stores its data where the node and index would be.  CELL_FLAG_ISSUE_HAS_NODE
// is what discerns the two categories, and can only be treated as a string
// when it has that flag.  Hence generically speaking, ISSUE! is not considered
// an ANY-SERIES? or ANY-STRING? type.
//
// However, there are UTF-8-based accessors VAL_UTF8_XXX which can be used to
// polymorphically access const data across ANY-STRING?, ANY-WORD?, and ISSUE!
//

INLINE bool IS_CHAR_CELL(const Cell* v) {
    if (Cell_Heart(v) != REB_ISSUE)
        return false;

    if (Get_Cell_Flag(v, ISSUE_HAS_NODE))
        return false;  // allocated form, too long to be a character

    return EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN] <= 1;  // codepoint
}

INLINE bool IS_CHAR(const Atom* v) {
    if (not Is_Issue(v))
        return false;
    return IS_CHAR_CELL(v);
}

INLINE Codepoint Cell_Codepoint(const Cell* v) {
    assert(Not_Cell_Flag(v, ISSUE_HAS_NODE));

    if (EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN] == 0)
        return 0;  // no '\0` bytes internal to series w/REB_TEXT "heart"

    assert(EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN] == 1);  // e.g. codepoint

    Codepoint c;
    Back_Scan_UTF8_Char_Unchecked(&c, PAYLOAD(Bytes, v).at_least_8);
    return c;
}

// !!! There used to be a cached size for the codepoint in the binary data,
// but with the "ISSUECHAR!" unification, wasting a byte for that on all forms
// seems like a bad idea for something so cheap to calculate.  But keep a
// separate entry point in case that cache comes back.
//
INLINE Byte Cell_Char_Encoded_Size(const Cell* v)
  { return Encoded_Size_For_Codepoint(Cell_Codepoint(v)); }

INLINE const Byte* VAL_CHAR_ENCODED(const Cell* v) {
    assert(Cell_Heart(v) == REB_ISSUE and Not_Cell_Flag(v, ISSUE_HAS_NODE));
    assert(EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN] <= 1);  // e.g. codepoint
    return PAYLOAD(Bytes, v).at_least_8;  // !!! '\0' terminated or not?
}

INLINE REBVAL *Init_Issue_Utf8(
    Cell* out,
    Utf8(const*) utf8,  // previously validated UTF-8 (maybe not null term?)
    Size size,
    REBLEN len  // while validating, you should have counted the codepoints
){
    if (size + 1 <= sizeof(PAYLOAD(Bytes, out)).at_least_8) {
        Reset_Unquoted_Header_Untracked(
            out,
            FLAG_HEART_BYTE(REB_ISSUE) | CELL_MASK_NO_NODES
        );
        memcpy(PAYLOAD(Bytes, out).at_least_8, utf8, size);
        PAYLOAD(Bytes, out).at_least_8[size] = '\0';
        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_USED] = size;
        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_LEN] = len;
    }
    else {
        String* str = Make_Sized_String_UTF8(cs_cast(utf8), size);
        assert(String_Len(str) == len);  // ^-- revalidates :-/ should match
        Freeze_Series(str);
        Init_Text(out, str);
        HEART_BYTE(out) = REB_ISSUE;
    }
    return cast(REBVAL*, out);
}


// If you know that a codepoint is good (e.g. it came from an ANY-STRING?)
// this routine can be used.
//
INLINE REBVAL *Init_Char_Unchecked_Untracked(Cell* out, Codepoint c) {
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_ISSUE) | CELL_MASK_NO_NODES
    );

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
        Size encoded_size = Encoded_Size_For_Codepoint(c);
        Encode_UTF8_Char(PAYLOAD(Bytes, out).at_least_8, c, encoded_size);
        PAYLOAD(Bytes, out).at_least_8[encoded_size] = '\0';  // terminate

        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_USED] = encoded_size;  // bytes
        EXTRA(Bytes, out).exactly_4[IDX_EXTRA_LEN] = 1;  // just one codepoint
    }

    HEART_BYTE(out) = REB_ISSUE;  // heart is TEXT, presents as issue
    assert(Cell_Codepoint(out) == c);
    return cast(REBVAL*, out);
}

#define Init_Char_Unchecked(out,c) \
    TRACK(Init_Char_Unchecked_Untracked((out), (c)))

INLINE Context* Maybe_Init_Char_Untracked(Cell* out, uint32_t c) {
    if (c > MAX_UNI) {
        DECLARE_LOCAL (temp);
        return Error_Codepoint_Too_High_Raw(Init_Integer(temp, c));
    }

    // !!! Should other values that can't be read be forbidden?  Byte order
    // mark?  UTF-16 surrogate stuff?  If something is not legitimate in a
    // UTF-8 codepoint stream, it shouldn't be used.

    Init_Char_Unchecked_Untracked(out, c);
    return nullptr;
}

#define Maybe_Init_Char(out,c) \
    Maybe_Init_Char_Untracked(TRACK(out), (c))


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
//         /out [blank! word! path! blackhole?]
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

#define Init_Blackhole(out) \
    Init_Char_Unchecked((out), 0)

INLINE bool Is_Blackhole(const Atom* v) {
    if (not IS_CHAR(v))
        return false;

    if (Cell_Codepoint(v) == 0)
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


// Analogous to VAL_BYTES_AT, some routines were willing to accept either an
// ANY-WORD? or an ANY-STRING? to get UTF-8 data.  This is a convenience
// routine for handling that.
//
INLINE Utf8(const*) Cell_Utf8_Len_Size_At_Limit(
    Option(Length*) length_out,
    Option(Size*) size_out,
    const Cell* v,
    REBINT limit
){
  #if !defined(NDEBUG)
    Size dummy_size;
    if (not size_out)
        size_out = &dummy_size;  // force size calculation for debug check
  #endif

    Heart heart = Cell_Heart(v);

    if (heart == REB_ISSUE and Not_Cell_Flag(v, ISSUE_HAS_NODE)) {
        REBLEN len;
        Size size;
        //
        // Note that unsigned cast of UNLIMITED as -1 to REBLEN is a large #
        //
        if (cast(REBLEN, limit) >= EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN]) {
            len = EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN];
            size = EXTRA(Bytes, v).exactly_4[IDX_EXTRA_USED];
        }
        else {
            len = 0;
            Utf8(const*) at = cast(Utf8(const*), PAYLOAD(Bytes, v).at_least_8);
            for (; limit != 0; --limit, ++len)
                at = Skip_Codepoint(at);
            size = at - PAYLOAD(Bytes, v).at_least_8;
        }

        if (length_out)
            *unwrap(length_out) = len;
        if (size_out)
            *unwrap(size_out) = size;
        return cast(Utf8(const*), PAYLOAD(Bytes, v).at_least_8);
    }

    const String* s = c_cast(String*, Cell_Node1(v));  // +Cell_Issue_String()
    Utf8(const*) utf8;

    if (Is_String_Symbol(s)) {
        utf8 = String_Head(s);

        if (size_out or length_out) {
            if (limit == UNLIMITED and not length_out)
                *unwrap(size_out) = String_Size(s);
            else {
                // Symbols don't cache their codepoint length, must calculate
                //
                // Note that signed cast to REBLEN of -1 UNLIMITED is a large #
                //
                Utf8(const*) cp = utf8;
                REBLEN index = 0;
                for (
                    ;
                    index < cast(REBLEN, limit);
                    ++index, cp = Skip_Codepoint(cp)
                ){
                    if (Codepoint_At(cp) == '\0')
                        break;
                }
                if (size_out)
                    *unwrap(size_out) = cp - utf8;
                if (length_out)
                    *unwrap(length_out) = index;
            }
        }
    }
    else if (heart == REB_ISSUE or heart == REB_URL) {  // no index
        utf8 = String_Head(s);
        if (size_out)
            *unwrap(size_out) = Series_Used(s);
        if (length_out)
            *unwrap(length_out) = s->misc.length;
    }
    else {
        utf8 = Cell_String_At(v);

        if (size_out or length_out) {
            Size utf8_size = Cell_String_Size_Limit_At(length_out, v, limit);
            if (size_out)
                *unwrap(size_out) = utf8_size;
            // length_out handled by Cell_String_Size_Limit_At, even if nullptr
        }
    }

    return utf8;
}

#define Cell_Utf8_Len_Size_At(length_out,size_out,v) \
    Cell_Utf8_Len_Size_At_Limit((length_out), (size_out), (v), UNLIMITED)

#define Cell_Utf8_Size_At(size_out,v) \
    Cell_Utf8_Len_Size_At_Limit(nullptr, (size_out), (v), UNLIMITED)

#define Cell_Utf8_At(v) \
    Cell_Utf8_Len_Size_At_Limit(nullptr, nullptr, (v), UNLIMITED)
