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
// `CODEPOINT OF` operation, or by using FIRST on the token.
//
// TYPE_ISSUE has two forms: one with a separate node allocation and one that
// stores data where a node and index would be.  Stringlike_Has_Node()
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
// * Historical Redbol supported a ^(NULL) codepoint, e.g. '\0', but Ren-C
//   deemed it to be not worth the trouble.  Only BLOB! can have embedded
//   zero bytes.  For strings it is termination only...so that only one return
//   result is needed from APIs like rebSpell().
//
//   All efforts are being made to make it as easy to work with a BLOB! on
//   string-like tasks where internal 0 bytes are ok.
//


//=//// CELL REPRESENTATION OF NUL CODEPOINT (USES #{00} BLOB!) ///////////=//
//
// Ren-C's unification of chars and "ISSUE!" to a single immutable stringlike
// type meant they could not physically contain a zero codepoint.
//
// It would be possible to declare the empty issue of #"" representing the
// NUL codepoint state.  But that would be odd, since inserting empty strings
// into other strings is considered to be legal and not change the string.
// saying that (insert "abc" #"") would generate an illegal-zero-byte error
// doesn't seem right.
//
// So to square this circle, the NUL state is chosen to be represented simply
// as the #{00} binary BLOB!.  That gives it the desired properties of an
// error if you try to insert it into a string, but still allowing you to
// insert it into blobs.
//
// To help make bring some uniformity to this, the CODEPOINT OF function
// will give back codepoints for binaries that represent UTF-8, including
// giving back 0 for #{00}.  CODEPOINT OF thus works on all strings, e.g.
// (codepoint of <A>) -> 65.  But the only way you can get 0 back is if you
// call it on #{00}
//

INLINE bool Is_Cell_NUL(const Cell* c) {
    if (Cell_Heart(c) != TYPE_BLOB)
        return false;

    Size size;
    const Byte* at = Cell_Blob_Size_At(&size, c);
    if (size != 1)
        return false;

    return *at == 0;
}

INLINE bool Is_NUL(const Atom* v) {
    if (QUOTE_BYTE(v) != NOQUOTE_1)
        return false;
    return Is_Cell_NUL(v);
}

INLINE bool IS_CHAR_CELL(const Cell* c) {
    if (Is_Cell_NUL(c))
        return true;

    if (Cell_Heart(c) != TYPE_ISSUE)
        return false;

    if (Stringlike_Has_Node(c))
        return false;  // allocated form, too long to be a character

    return c->extra.at_least_4[IDX_EXTRA_LEN] == 1;  // codepoint
}

INLINE bool IS_CHAR(const Atom* v) {
    if (QUOTE_BYTE(v) != NOQUOTE_1)
        return false;
    return IS_CHAR_CELL(v);
}

INLINE Codepoint Cell_Codepoint(const Cell* c) {  // must pass IS_CHAR_CELL()
    if (Is_Cell_NUL(c))
        return 0;

    assert(Cell_Heart(c) == TYPE_ISSUE);
    assert(not Stringlike_Has_Node(c));

    assert(c->extra.at_least_4[IDX_EXTRA_LEN] == 1);  // e.g. char

    Codepoint codepoint;
    Back_Scan_Utf8_Char_Unchecked(&codepoint, c->payload.at_least_8);
    return codepoint;
}

INLINE bool Try_Init_Small_Utf8_Untracked(
    Init(Element) out,
    Heart heart,
    Utf8(const*) utf8,  // previously validated UTF-8, may not be null term
    Length len,
    Size size
){
    assert(
        Any_Utf8_Type(heart)
        and not Any_String_Type(heart) and not Any_Word_Type(heart)
    );
    assert(len <= size);
    if (size + 1 > Size_Of(out->payload.at_least_8))
        return false;
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART_BYTE(heart) | CELL_MASK_NO_NODES
    );
    memcpy(&out->payload.at_least_8, utf8, size + 1);  // copy '\0' term
    out->payload.at_least_8[size] = '\0';
    out->extra.at_least_4[IDX_EXTRA_USED] = size;
    out->extra.at_least_4[IDX_EXTRA_LEN] = len;
    return true;
}

#define Try_Init_Small_Utf8(out,heart,utf8,len,size) \
    Try_Init_Small_Utf8_Untracked(TRACK(out), (heart), (utf8), (len), (size))

INLINE Element* Init_Utf8_Non_String(
    Init(Element) out,
    Heart heart,
    Utf8(const*) utf8,  // previously validated UTF-8 (maybe not null term)
    Size size,
    Length len  // while validating, you should have counted the codepoints
){
    if (Try_Init_Small_Utf8_Untracked(out, heart, utf8, len, size))
        return out;

    String* str = Make_Sized_String_UTF8(cs_cast(utf8), size);
    assert(String_Len(str) == len);  // ^-- revalidates :-/ should match
    Freeze_Flex(str);
    Init_Any_String(out, heart, str);
    return out;
}

#define Init_Email(out,utf8,size,len) \
    Init_Utf8_Non_String((out), TYPE_EMAIL, (utf8), (size), (len))

#define Init_Url(out,utf8,size,len) \
    Init_Utf8_Non_String((out), TYPE_URL, (utf8), (size), (len))

#define Init_Issue(out,utf8,size,len) \
    Init_Utf8_Non_String((out), TYPE_ISSUE, (utf8), (size), (len))


// If you know that a codepoint is good (e.g. it came from an ANY-STRING?)
// this routine can be used.
//
INLINE Element* Init_Char_Unchecked_Untracked(Init(Element) out, Codepoint c) {
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART_BYTE(TYPE_ISSUE) | CELL_MASK_NO_NODES
    );

    if (c == 0) {  // NUL is #{00}, a BLOB! not an ISSUE! (see Is_NUL())
        Copy_Cell(out, cast(const Element*, LIB(NUL)));
    }
    else {
        Size encoded_size = Encoded_Size_For_Codepoint(c);
        Encode_UTF8_Char(out->payload.at_least_8, c, encoded_size);
        out->payload.at_least_8[encoded_size] = '\0';  // terminate

        out->extra.at_least_4[IDX_EXTRA_USED] = encoded_size;  // bytes
        out->extra.at_least_4[IDX_EXTRA_LEN] = 1;  // just one codepoint
        HEART_BYTE(out) = TYPE_ISSUE;  // heart is TEXT, presents as issue
    }

    assert(Cell_Codepoint(out) == c);
    return out;
}

#define Init_Char_Unchecked(out,c) \
    TRACK(Init_Char_Unchecked_Untracked((out), (c)))

// 1. The "codepoint too high" error was once parameterized with the large
//    value, but see Startup_Utf8_Errors() for why these need to be cheap
//
INLINE Option(Error*) Trap_Init_Char_Untracked(Cell* out, uint32_t c) {
    if (c > MAX_UNI)
        return Cell_Error(g_error_codepoint_too_high);  // no parameter [1]

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

  #if RUNTIME_CHECKS
    Size dummy_size;
    if (not size_out)
        size_out = &dummy_size;  // force size calculation for debug check
  #endif

    if (not Stringlike_Has_Node(v)) {  // SIGIL!, some ISSUE!...
        assert(not Any_String_Type(Cell_Heart(v)));

        REBLEN len;
        Size size;
        if (
            not limit
            or *(unwrap limit) >= v->extra.at_least_4[IDX_EXTRA_LEN]
        ){
            len = v->extra.at_least_4[IDX_EXTRA_LEN];
            size = v->extra.at_least_4[IDX_EXTRA_USED];
        }
        else {
            len = 0;
            Utf8(const*) at = cast(Utf8(const*), v->payload.at_least_8);
            for (; len < *(unwrap limit); ++len)
                at = Skip_Codepoint(at);
            size = at - v->payload.at_least_8;
        }

        if (length_out)
            *(unwrap length_out) = len;
        if (size_out)
            *(unwrap size_out) = size;
        return cast(Utf8(const*), v->payload.at_least_8);
    }

    Utf8(const*) utf8 = Cell_String_At(v);

    if (size_out or length_out) {
        Size utf8_size = Cell_String_Size_Limit_At(length_out, v, limit);
        if (size_out)
            *(unwrap size_out) = utf8_size;
    }

    return utf8;
}

#define Cell_Utf8_Len_Size_At(length_out,size_out,v) \
    Cell_Utf8_Len_Size_At_Limit((length_out), (size_out), (v), UNLIMITED)

#define Cell_Utf8_Size_At(size_out,v) \
    Cell_Utf8_Len_Size_At_Limit(nullptr, (size_out), (v), UNLIMITED)

#define Cell_Utf8_At(v) \
    Cell_Utf8_Len_Size_At_Limit(nullptr, nullptr, (v), UNLIMITED)
