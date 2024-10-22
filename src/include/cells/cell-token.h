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
// stores data where a node and index would be.  CELL_FLAG_STRINGLIKE_HAS_NODE
// is what discerns the two categories, and can only be treated as a string
// when it has that flag.  Hence generically speaking, ISSUE! is not considered
// an ANY-SERIES? or ANY-STRING? type.
//
// However, there are UTF-8-based accessors VAL_UTF8_XXX which can be used to
// polymorphically access const data across ANY-STRING?, ANY-WORD?, and ISSUE!
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * In addition to the encoded bytes of the UTF-8, a single-codepoint ISSUE!
//   will also cache that codepoint.  Hence a CHAR? cell has both the UTF-8
//   representation and the codepoint on hand locally in the cell.
//
// * The 0 codepoint ("NUL") is a valid ISSUE! -but- it can not appear in an
//   ANY-STRING?.  Only BINARY! can have embedded zero bytes.  For strings it
//   is kept for termination, so that only one return result is needed from
//   APIs like rebSpell().  All efforts are being made to make it as easy to
//   work with a BINARY! on string-like tasks where internal 0 bytes are ok.
//


INLINE bool IS_CHAR_CELL(const Cell* v) {
    if (Cell_Heart(v) != REB_ISSUE)
        return false;

    if (Get_Cell_Flag(v, STRINGLIKE_HAS_NODE))
        return false;  // allocated form, too long to be a character

    return EXTRA(Bytes, v).at_least_4[IDX_EXTRA_LEN] <= 1;  // codepoint
}

INLINE bool IS_CHAR(const Atom* v) {
    if (not Is_Issue(v))
        return false;
    return IS_CHAR_CELL(v);
}

INLINE Codepoint Cell_Codepoint(const Cell* v) {
    assert(Not_Cell_Flag(v, STRINGLIKE_HAS_NODE));

    if (EXTRA(Bytes, v).at_least_4[IDX_EXTRA_LEN] == 0)
        return 0;  // no '\0` bytes internal to ANY-UTF8! series

    assert(EXTRA(Bytes, v).at_least_4[IDX_EXTRA_LEN] == 1);  // e.g. codepoint

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
    assert(Cell_Heart(v) == REB_ISSUE);
    assert(Not_Cell_Flag(v, STRINGLIKE_HAS_NODE));
    assert(EXTRA(Bytes, v).at_least_4[IDX_EXTRA_LEN] <= 1);  // e.g. codepoint
    return PAYLOAD(Bytes, v).at_least_8;  // !!! '\0' terminated or not?
}

INLINE Element* Init_Issue_Utf8(
    Sink(Element) out,
    Utf8(const*) utf8,  // previously validated UTF-8 (maybe not null term?)
    Size size,
    Length len  // while validating, you should have counted the codepoints
){
    if (size + 1 <= Size_Of(PAYLOAD(Bytes, out).at_least_8)) {
        Reset_Cell_Header_Untracked(
            out,
            FLAG_HEART_BYTE(REB_ISSUE) | CELL_MASK_NO_NODES
        );
        memcpy(PAYLOAD(Bytes, out).at_least_8, utf8, size);
        PAYLOAD(Bytes, out).at_least_8[size] = '\0';
        EXTRA(Bytes, out).at_least_4[IDX_EXTRA_USED] = size;
        EXTRA(Bytes, out).at_least_4[IDX_EXTRA_LEN] = len;
    }
    else {
        String* str = Make_Sized_String_UTF8(cs_cast(utf8), size);
        assert(String_Len(str) == len);  // ^-- revalidates :-/ should match
        Freeze_Flex(str);
        Init_Text(out, str);
        HEART_BYTE(out) = REB_ISSUE;
    }
    return out;
}


// If you know that a codepoint is good (e.g. it came from an ANY-STRING?)
// this routine can be used.
//
INLINE Element* Init_Char_Unchecked_Untracked(Sink(Element) out, Codepoint c) {
    Reset_Cell_Header_Untracked(
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
        EXTRA(Bytes, out).at_least_4[IDX_EXTRA_USED] = 0;
        EXTRA(Bytes, out).at_least_4[IDX_EXTRA_LEN] = 0;
        PAYLOAD(Bytes, out).at_least_8[0] = '\0';  // terminate
    }
    else {
        Size encoded_size = Encoded_Size_For_Codepoint(c);
        Encode_UTF8_Char(PAYLOAD(Bytes, out).at_least_8, c, encoded_size);
        PAYLOAD(Bytes, out).at_least_8[encoded_size] = '\0';  // terminate

        EXTRA(Bytes, out).at_least_4[IDX_EXTRA_USED] = encoded_size;  // bytes
        EXTRA(Bytes, out).at_least_4[IDX_EXTRA_LEN] = 1;  // just one codepoint
    }

    HEART_BYTE(out) = REB_ISSUE;  // heart is TEXT, presents as issue
    assert(Cell_Codepoint(out) == c);
    return out;
}

#define Init_Char_Unchecked(out,c) \
    TRACK(Init_Char_Unchecked_Untracked((out), (c)))

INLINE Option(Error*) Trap_Init_Char_Untracked(Cell* out, uint32_t c) {
    if (c > MAX_UNI) {
        DECLARE_ATOM (temp);
        return Error_Codepoint_Too_High_Raw(Init_Integer(temp, c));
    }

    // !!! Should other values that can't be read be forbidden?  Byte order
    // mark?  UTF-16 surrogate stuff?  If something is not legitimate in a
    // UTF-8 codepoint stream, it shouldn't be used.

    Init_Char_Unchecked_Untracked(out, c);
    return nullptr;
}

#define Trap_Init_Char(out,c) \
    Trap_Init_Char_Untracked(TRACK(out), (c))

#define Init_Space(out) \
    Init_Char_Unchecked((out), ' ')

INLINE bool Is_Space(const Value* v) {
    if (not IS_CHAR(v))
        return false;

    return Cell_Codepoint(v) == ' ';
}


//=//// GENERIC UTF-8 ACCESSORS //////////////////////////////////////////=//


// Analogous to VAL_BYTES_AT, some routines were willing to accept either an
// ANY-WORD? or an ANY-STRING? to get UTF-8 data.  This is a convenience
// routine for handling that.
//
INLINE Utf8(const*) Cell_Utf8_Len_Size_At_Limit(
    Option(Sink(Length)) length_out,
    Option(Sink(Size)) size_out,
    const Cell* v,
    Option(const Length*) limit  // nullptr means no limit
){
    if (limit)
        assert(*(unwrap limit) >= 0);

  #if !defined(NDEBUG)
    Size dummy_size;
    if (not size_out)
        size_out = &dummy_size;  // force size calculation for debug check
  #endif

    Heart heart = Cell_Heart(v);

    if (Not_Cell_Flag(v, STRINGLIKE_HAS_NODE)) {  // SIGIL!, some ISSUE!...
        REBLEN len;
        Size size;
        if (
            not limit
            or *(unwrap limit) >= EXTRA(Bytes, v).at_least_4[IDX_EXTRA_LEN]
        ){
            len = EXTRA(Bytes, v).at_least_4[IDX_EXTRA_LEN];
            size = EXTRA(Bytes, v).at_least_4[IDX_EXTRA_USED];
        }
        else {
            len = 0;
            Utf8(const*) at = cast(Utf8(const*), PAYLOAD(Bytes, v).at_least_8);
            for (; len < *(unwrap limit); ++len)
                at = Skip_Codepoint(at);
            size = at - PAYLOAD(Bytes, v).at_least_8;
        }

        if (length_out)
            *(unwrap length_out) = len;
        if (size_out)
            *(unwrap size_out) = size;
        return cast(Utf8(const*), PAYLOAD(Bytes, v).at_least_8);
    }

    const String* s = c_cast(String*, Cell_Node1(v));  // +Cell_Issue_String()
    Utf8(const*) utf8;

    if (heart == REB_ISSUE or heart == REB_URL) {  // no index
        utf8 = String_Head(s);
        if (size_out)
            *(unwrap size_out) = Flex_Used(s);
        if (length_out) {
            if (not limit)
                *(unwrap length_out) = s->misc.length;
            else
                *(unwrap length_out) = MIN(s->misc.length, *(unwrap limit));
        }
    }
    else {
        utf8 = Cell_String_At(v);

        if (size_out or length_out) {
            Size utf8_size = Cell_String_Size_Limit_At(length_out, v, limit);
            if (size_out)
                *(unwrap size_out) = utf8_size;
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
