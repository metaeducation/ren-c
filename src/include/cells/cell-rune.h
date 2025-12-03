//
//  file: %cell-rune.h
//  summary: "Definitions for an Immutable Sequence of 1 to N Codepoints"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// RUNE! merges two of Rebol's historical types: CHAR! and ISSUE!.  Due to
// the merging, single codepoints can often be represented without delimiters:
//
//     >> second "abc"
//     == #b  ; instead of #"b" CHAR! (not ISSUE!) in historical Rebol
//
// As with ISSUE!, multiple codepoint runes are legal.
//
// If possible, runes store encoded UTF-8 data entirely in a Cell...saving
// on allocations and improving locality.  In this system, a "character" is
// simply a single-length RUNE!, which is translated to a codepoint using the
// `CODEPOINT OF` operation, or by using FIRST on the rune.
//
// TYPE_RUNE has two forms: one with a Strand* allocation, and one that stores
// content data where a Strand* and index would be.  Stringlike_Has_Stub()
// is what discerns the two categories, and can only be treated as a string
// when it has that flag.  Hence generically speaking, RUNE! is not considered
// an ANY-SERIES? or ANY-STRING? type.
//
// However, there are UTF-8-based accessors like Cell_Utf8_At() which can
// polymorphically access const data across ANY-STRING?, ANY-WORD?, and RUNE!
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Historical Redbol supported a ^(NULL) codepoint, e.g. '\0', but Ren-C
//   deemed it to be not worth the trouble.  Only BLOB! can have embedded
//   zero bytes.  For strings it is termination only...so that only one return
//   result is needed from APIs like rebSpell().
//
//   All efforts are being made to make it as easy to work with a BLOB! on
//   string-like tasks where internal 0 bytes are ok.
//

//=//// SINGLE CODEPOINT RUNE FLAG ////////////////////////////////////////=//
//
// This allows the CHAR? type-constraint of single-character RUNE! to be
// a test of the header bits only, without checking the payload or extra.
// It's a minor speedup, but everything helps.
//
// !!! When CHAR! was a separate datatype, it stored the codepoint in the
// payload and the encoding in the Cell->extra.  When RUNE! generalized, it
// stored the encoded form in the Cell->payload.at_least_8, and stuck the
// length in Byte of Cell->extra.at_least_4.  There are strategies which
// could still store the codepoint and get the size and length information
// other ways.  Review if getting the codepoint without decoding is worth it.
//
#define CELL_FLAG_RUNE_ONE_CODEPOINT  CELL_FLAG_TYPE_SPECIFIC_A


//=//// CELL_FLAG_RUNE_IS_SPACE ////////////////////////////////////////////=//
//
// The space variations of [_ ~ @ $ ^] are common, as is the antiform of
// TRIPWIRE.  Being able to test for these just by looking at the header has
// advantages, similar to the CELL_FLAG_RUNE_ONE_CODEPOINT.
//
#define CELL_FLAG_RUNE_IS_SPACE  CELL_FLAG_TYPE_SPECIFIC_B


INLINE bool Rune_Is_Single_Codepoint(const Cell* cell) {
    assert(Unchecked_Heart_Of(cell) == TYPE_RUNE);
    return Get_Cell_Flag(cell, RUNE_ONE_CODEPOINT);
}

INLINE bool Is_Rune_And_Is_Char(const Value* v) {
    return (
        (Ensure_Readable(v)->header.bits & (
            CELL_MASK_HEART_AND_SIGIL_AND_LIFT | CELL_FLAG_RUNE_ONE_CODEPOINT
        )) == (
            FLAG_HEART(TYPE_RUNE)
                | FLAG_LIFT_BYTE(NOQUOTE_2)
                | CELL_FLAG_RUNE_ONE_CODEPOINT
        )
    );
}

INLINE Codepoint Rune_Known_Single_Codepoint(const Cell* cell) {
    assert(
        Unchecked_Heart_Of(cell) == TYPE_RUNE
        and not Stringlike_Has_Stub(cell)
        and cell->extra.at_least_4[IDX_EXTRA_LEN] == 1
    );
    Codepoint c;
    Back_Scan_Utf8_Char_Unchecked(&c, cell->payload.at_least_8);
    assert(c != '\0');
    return c;
}

INLINE Option(Byte) First_Byte_Of_Rune_If_Single_Char(const Cell* cell) {
    if (not Rune_Is_Single_Codepoint(cell))
        return '\0';

    possibly(Is_Utf8_Lead_Byte(cell->payload.at_least_8[0]));
    return cell->payload.at_least_8[0];
}

INLINE Option(Codepoint) Codepoint_Of_Rune_If_Single_Char(const Cell* cell)
{
    if (not Rune_Is_Single_Codepoint(cell))
        return '\0';
    return Rune_Known_Single_Codepoint(cell);
}

INLINE Result(Codepoint) Get_Rune_Single_Codepoint(const Cell* cell){
    if (not Rune_Is_Single_Codepoint(cell))
        return fail (
            "Can't get Codepoint if RUNE! has more than one character"
        );

    return Rune_Known_Single_Codepoint(cell);
}


//=//// INITIALIZATION ////////////////////////////////////////////////////=//

INLINE bool Try_Init_Small_Utf8_Untracked(
    Init(Element) out,
    Heart heart,
    Utf8(const*) utf8,  // previously validated UTF-8, may not be null term
    Length len,
    Size size
){
    assert(
        Any_Utf8_Type(heart)
        and not Any_String_Type(heart) and heart != TYPE_WORD
    );
    assert(len <= size);
    if (size + 1 > Size_Of(out->payload.at_least_8))
        return false;

    const Byte* bp = utf8;

    Reset_Cell_Header_Noquote(  // include fast flags for space/char checks
        out,
        FLAG_HEART(heart) | CELL_MASK_NO_MARKING
            | ((len == 1) ? CELL_FLAG_RUNE_ONE_CODEPOINT : 0)
            | ((size == 1) and (bp[0] == ' ') ? CELL_FLAG_RUNE_IS_SPACE : 0)
    );

    memcpy(
        &out->payload.at_least_8,
        bp,
        size + 1  // copy '\0' term
    );
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

    Strand* str = Make_Sized_Strand_UTF8(s_cast(utf8), size);
    assert(Strand_Len(str) == len);  // ^-- revalidates :-/ should match
    Freeze_Flex(str);
    Init_Any_String(out, heart, str);
    return out;
}

#define Init_Email(out,utf8,size,len) \
    Init_Utf8_Non_String((out), TYPE_EMAIL, (utf8), (size), (len))

#define Init_Url(out,utf8,size,len) \
    Init_Utf8_Non_String((out), TYPE_URL, (utf8), (size), (len))

#define Init_Rune(out,utf8,size,len) \
    Init_Utf8_Non_String((out), TYPE_RUNE, (utf8), (size), (len))


// If you know that a codepoint is good (e.g. it came from an ANY-STRING?)
// this routine can be used.
//
INLINE Element* Init_Char_Unchecked_Untracked(Init(Element) out, Codepoint c) {
    assert(c != '\0');  // NUL is #{00} (see Is_Blob_And_Is_Zero())
    assert(not (c >= UNI_SUR_HIGH_START and c <= UNI_SUR_LOW_END));
    assert(not (c > UNI_MAX_LEGAL_UTF32));

    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(TYPE_RUNE) | CELL_MASK_NO_MARKING
            | CELL_FLAG_RUNE_ONE_CODEPOINT
            | ((c == ' ') ? CELL_FLAG_RUNE_IS_SPACE : 0)
    );

    Size encoded_size = Encoded_Size_For_Codepoint(c);
    Encode_UTF8_Char(out->payload.at_least_8, c, encoded_size);
    out->payload.at_least_8[encoded_size] = '\0';  // terminate

    out->extra.at_least_4[IDX_EXTRA_USED] = encoded_size;  // bytes
    out->extra.at_least_4[IDX_EXTRA_LEN] = 1;  // just one codepoint

    assert(Rune_Known_Single_Codepoint(out) == c);
    return out;
}

#define Init_Char_Unchecked(out,c) \
    TRACK(Init_Char_Unchecked_Untracked((out), (c)))

// 1. The "codepoint too high" error was once parameterized with the large
//    value, but see Startup_Utf8_Errors() for why these need to be cheap
//
INLINE Result(Element*) Init_Single_Codepoint_Rune_Untracked(
    Init(Element) out,
    uint32_t c
){
    if (c > MAX_UNI)
        return fail (Cell_Error(g_error_codepoint_too_high));  // no param [1]

    if (c >= UNI_SUR_HIGH_START and c <= UNI_SUR_LOW_END)
        return fail (Cell_Error(g_error_no_utf8_surrogates));  // param [1]

    // !!! Should other values that can't be read be forbidden?  Byte order
    // mark?  UTF-16 surrogate stuff?  If something is not legitimate in a
    // UTF-8 codepoint stream, it shouldn't be used.

    return Init_Char_Unchecked_Untracked(out, c);
}

#define Init_Single_Codepoint_Rune(out,c) \
    Init_Single_Codepoint_Rune_Untracked(TRACK(out), (c))


//=//// SPACE RUNES ///////////////////////////////////////////////////////=//
//
// Space runes are inert in the evaluator, and represented by an underscore.
// They are used as agnostic placeholders.
//
//    >> append [a b c] _
//    == [a b c _]
//
// Space takes on some placeholder responsibilities of Rebol2's NONE!
// value, while the "soft failure" aspects are covered by NULL (which unlike
// blanks, can't be stored in blocks).  Consequently spaces are not "falsey"
// which means all "reified" values that can be stored in blocks are
// conditionally true.
//
//     >> if fourth [a b c _] [print "Spaces are truthy"]
//     Spaces are truthy
//
// 1. Instead of rendering as `@_` and `^_` and `$_`, a Sigil'd space will
//    render as `@`, `^`, and `$`.

#define Init_Space(out) \
    Init_Char_Unchecked((out), ' ')

#define Init_Sigiled_Space(out,sigil) \
    Sigilize(Init_Space(out), sigil)

#define Is_Space_Underlying(v) \
    (Ensure_Readable(v)->header.bits & ( \
        CELL_MASK_HEART_NO_SIGIL | CELL_FLAG_RUNE_IS_SPACE \
    )) == ( \
        FLAG_HEART(TYPE_RUNE) | CELL_FLAG_RUNE_IS_SPACE \
    )

INLINE bool Any_Sigiled_Space(const Element* e) {
    if (LIFT_BYTE(e) != NOQUOTE_2 or not Sigil_Of(e))
        return false;
    return Is_Space_Underlying(e);
}

INLINE bool Is_Space_With_Lift_Sigil(
    LiftByte lift,
    Option(Sigil) sigil,
    const Value* v
){
    return (
        (Ensure_Readable(v)->header.bits & (
            CELL_MASK_HEART_AND_SIGIL_AND_LIFT | CELL_FLAG_RUNE_IS_SPACE
        )) == (
            FLAG_HEART(TYPE_RUNE)
                | FLAG_LIFT_BYTE(lift)
                | FLAG_SIGIL(sigil)
                | CELL_FLAG_RUNE_IS_SPACE
        )
    );
}

#define Is_Space(v) \
    Is_Space_With_Lift_Sigil(NOQUOTE_2, SIGIL_0, (v))  // renders as `_` [1]

#define Is_Pinned_Space(v) \
    Is_Space_With_Lift_Sigil(NOQUOTE_2, SIGIL_PIN, (v))  // renders as `@` [1]

#define Is_Metaform_Space(v) \
    Is_Space_With_Lift_Sigil(NOQUOTE_2, SIGIL_META, (v))  // renders as `^` [1]

#define Is_Tied_Space(v) \
    Is_Space_With_Lift_Sigil(NOQUOTE_2, SIGIL_TIE, (v))  // renders as `$` [1]


//=//// '~' QUASIFORM (a.k.a. QUASAR) /////////////////////////////////////=//
//
// The quasiform of space is a tilde (instead of ~_~), and called QUASAR
//
//    >> lift print "Quasiform of SPACE is QUASAR"
//    Quasiform of SPACE is QUASAR
//    == ~
//
// !!! At one point it was very fast to initialize a QUASAR, as it could be
// done with only the header.  Consider the idea of making character literals
// able to be initialized with just the header for space-like cases.
//

#define Is_Quasar(v) \
    Is_Space_With_Lift_Sigil(QUASIFORM_3, SIGIL_0, (v))

INLINE Element* Init_Quasar_Untracked(Init(Element) out) {
    Init_Char_Unchecked_Untracked(out, ' ');  // use space as the base
    Quasify_Isotopic_Fundamental(out);
    assert(Is_Quasar(out));
    return out;
}

#define Init_Quasar(out) \
    TRACK(Init_Quasar_Untracked(out))


//=//// '~' ANTIFORM (a.k.a. TRIPWIRE) ////////////////////////////////////=//
//
// All RUNE! values have antiforms, that are considered to be TRASH!.
//
// The antiform of SPACE is a particularly succinct trash state, called
// TRIPWIRE.  It's a quick way to make a variable
//  * Quick way to unset variables, simply `(var: ~)`
//

INLINE bool Is_Tripwire(Need(const Value*) v)  // don't allow Element*
 { return Is_Space_With_Lift_Sigil(ANTIFORM_1, SIGIL_0, v); }

INLINE Value* Init_Tripwire_Untracked(Init(Value) out) {
    Init_Char_Unchecked_Untracked(out, ' ');  // use space as the base
    Stably_Antiformize_Unbound_Fundamental(out);
    assert(Is_Tripwire(out));
    return out;
}

#define Init_Tripwire(out) \
    TRACK(Init_Tripwire_Untracked(out))

#define Init_Lifted_Tripwire(out) \
    Init_Quasar(out)


//=//// GENERIC UTF-8 ACCESSORS //////////////////////////////////////////=//
//
// Analogous to Cell_Bytes_At(), this allows you to get read-only UTF-8 data
// out of ANY-WORD?, ANY-STRING?, or a RUNE!
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

    if (not Stringlike_Has_Stub(v)) {  // SIGIL!, some RUNE!...
        assert(not Any_String_Type(Heart_Of(v)));

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
            Utf8(const*) at = u_cast(Utf8(const*), v->payload.at_least_8);
            for (; len < *(unwrap limit); ++len)
                at = Skip_Codepoint(at);
            size = at - v->payload.at_least_8;
        }

        if (length_out)
            *(unwrap length_out) = len;
        if (size_out)
            *(unwrap size_out) = size;
        return u_cast(Utf8(const*), v->payload.at_least_8);
    }

    Utf8(const*) utf8 = String_At(v);

    if (size_out or length_out) {
        Size utf8_size = String_Size_Limit_At(length_out, v, limit);
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
