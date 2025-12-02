//
//  file: %t-char.c
//  summary: "character datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// See %sys-char.h for notes.

#include "sys-core.h"

#undef Byte  // sys-zlib.h defines it compatibly (unsigned char)
#include "sys-zlib.h"  // for crc32_z()


// Index into the table below with the first byte of a UTF-8 sequence to
// get the number of trailing bytes that are supposed to follow it.
// Note that *legal* UTF-8 values can't have 4 or 5-bytes. The table is
// left as-is for anyone who may want to do such conversion, which was
// allowed in earlier algorithms.
//
const char g_trailing_bytes_for_utf8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};


// Magic values subtracted from a buffer value during UTF8 conversion.
// This table contains as many values as there might be trailing bytes
// in a UTF-8 sequence.
//
const uint_fast32_t g_offsets_from_utf8[6] = {
    0x00000000UL, 0x00003080UL, 0x000E2080UL,
    0x03C82080UL, 0xFA082080UL, 0x82082080UL
};


// Once the bits are split out into bytes of UTF-8, this is a mask OR-ed
// into the first byte, depending on how many bytes follow.  There are
// as many entries in this table as there are UTF-8 sequence types.
// (I.e., one byte sequence, two byte... etc.). Remember that sequencs
// for *legal* UTF-8 will be 4 or fewer bytes total.
//
const uint_fast8_t g_first_byte_mark_utf8[7] = {
    0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC
};


//
//  Back_Scan_Utf8_Char: C
//
// Decodes a single encoded UTF-8 codepoint and updates the position *at the
// the last byte of the character's data*.  (This differs from the usual
// `Scan_XXX` interface of returning the position after the scanned element,
// ready to read the next one.)
//
// The peculiar interface is useful in loops that process ordinary ASCII chars
// directly -as well- as UTF-8 ones.  The loop can do a single byte pointer
// increment after both kinds of elements, avoiding the need to call any kind
// of `Scan_Ascii()`:
//
//     for (; size > 0; ++bp, --size) {
//         if (Is_Byte_Ascii(*bp)) {
//             // do ASCII stuff...
//         }
//         else {
//             require (
//               Codepoint c = Back_Scan_Utf8_Char(&bp, &size)
//             );
//             // do UNICODE stuff...
//         }
//     }
//
// The third parameter is an optional size that will be decremented by
// the number of "extra" bytes the UTF8 has beyond a single byte character.
// This allows for decrement-style loops such as the above.
//
// If failure due to insufficient data or malformed bytes, then an error is
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
//      https://github.com/rebol/rebol-runes/runes/638
//      https://en.wikipedia.org/wiki/UTF-8#Overlong_encodings
//
//    ...which meant that various illegal input patterns would be tolerated,
//    so long as they didn't cause crashes.  You would just not have the
//    input validated, and get garbage characters out.  The Ren-C philosophy
//    is that since this check only applies to non-ASCII, it is worth it to
//    do the validation.  And it only applies when scanning strings...once
//    they are loaded into Strand* we use Back_Scan_Utf8_Char_Unchecked().
//
// 3. We want the erroring cases to be inexpensive, because UTF-8 characters
//    are scanned for instance in FIND of a TEXT! in a binary BLOB! which may
//    have lots of invalid UTF-8.  So all the errors used here are
//    pre-allocated.  But those allocations only happen once the error
//    machinery is ready.
//
Result(Codepoint) Back_Scan_Utf8_Char(  // no NUL or substitution chars [1]
    const Byte** bp,  // left alone if error result, "back updated" if not
    Option(Need(Size*)) size  // decremented in non-error case
){
    Codepoint c = 0;

    const Byte* source = *bp;
    uint_fast8_t trail = g_trailing_bytes_for_utf8[*source];

    if (size) {  // Check that we have enough valid source bytes
        if (cast(Size, trail + 1) > *(unwrap size))
            return fail (Cell_Error(g_error_utf8_too_short));  // cached [3]
    }
    else if (trail != 0) {
        do {
            if (source[trail] < 0x80)
                return fail (Cell_Error(g_error_utf8_trail_bad_bit));  // [3]
        } while (--trail != 0);

        trail = g_trailing_bytes_for_utf8[*source];
    }

    if (not Is_Legal_UTF8(source, trail + 1))  // was omitted in R3-Alpha [2]
        return fail (Cell_Error(g_error_overlong_utf8));  // cached [3]

    switch (trail) {
        case 5: c += *source++; c <<= 6;  // falls through
        case 4: c += *source++; c <<= 6;  // falls through
        case 3: c += *source++; c <<= 6;  // falls through
        case 2: c += *source++; c <<= 6;  // falls through
        case 1: c += *source++; c <<= 6;  // falls through
        case 0: c += *source++;
    }
    c -= g_offsets_from_utf8[trail];

    if (c > UNI_MAX_LEGAL_UTF32)
        return fail (Cell_Error(g_error_codepoint_too_high));  // cached [3]
    if (c >= UNI_SUR_HIGH_START and c <= UNI_SUR_LOW_END)
        return fail (Cell_Error(g_error_no_utf8_surrogates));  // cached [3]

    if (c == 0)  // string types disallow internal 0 bytes in Ren-C [1]
        return fail (Cell_Error(g_error_illegal_zero_byte));  // cached [3]

    if (size)
        *(unwrap size) -= trail;

    *bp += trail;
    return c;
}


//
//  CT_Utf8: C
//
// 1. As the replacement for CHAR!, RUNE! inherits the behavior that there
//    are no non-strict comparisons.  To compare non-strictly, they must be
//    aliased as TEXT!.  (!!! This should be reviewed.)
//
REBINT CT_Utf8(const Element* a, const Element* b, bool strict)
{
    assert(Any_Utf8_Type(Heart_Of(a)));
    assert(Any_Utf8_Type(Heart_Of(b)));

    if (Heart_Of(a) == TYPE_RUNE or Heart_Of(b) == TYPE_RUNE)
        strict = true;  // always true? [1]

    REBLEN l1;
    Utf8(const*) cp1 = Cell_Utf8_Len_Size_At(&l1, nullptr, a);

    REBLEN l2;
    Utf8(const*) cp2 = Cell_Utf8_Len_Size_At(&l2, nullptr, b);

    REBLEN len = MIN(l1, l2);

    for (; len > 0; len--) {
        Codepoint c1;
        Codepoint c2;

        cp1 = Utf8_Next(&c1, cp1);
        cp2 = Utf8_Next(&c2, cp2);

        REBINT d;
        if (strict)
            d = Cast_Signed(c1) - Cast_Signed(c2);
        else
            d = Cast_Signed(LO_CASE(c1)) - Cast_Signed(LO_CASE(c2));

        if (d != 0)
            return d > 0 ? 1 : -1;
    }

    if (l1 == l2)
        return 0;

    return l1 > l2 ? 1 : -1;
}


IMPLEMENT_GENERIC(EQUAL_Q, Any_Utf8)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Utf8(v1, v2, strict) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Any_Utf8)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Utf8(v1, v2, true) == -1);
}


IMPLEMENT_GENERIC(MAKE, Any_Utf8)
{
    INCLUDE_PARAMS_OF_MAKE;

    Heart heart = Datatype_Builtin_Heart(ARG(TYPE));
    assert(Any_Utf8_Type(heart));

    Element* arg = Element_ARG(DEF);

    switch (opt Type_Of(arg)) {
      case TYPE_INTEGER: {
        if (heart != TYPE_RUNE)
            panic ("Only RUNE! can MAKE a UTF-8 immutable type with INTEGER!");

        REBINT n = Int32(arg);
        trap (
          Init_Single_Codepoint_Rune(OUT, n)
        );
        return OUT; }

      case TYPE_BLOB: {
        if (heart != TYPE_RUNE)
            panic ("Only RUNE! can MAKE a UTF-8 immutable type with BLOB!");

        Size size;
        const Byte* bp = Blob_Size_At(&size, arg);
        if (size == 0)
            goto bad_make;

        Codepoint c;
        if (Is_Byte_Ascii(*bp)) {
            if (size != 1) {
                Copy_Cell(ARG(TYPE), Datatype_From_Type(TYPE_RUNE));
                return GENERIC_CFUNC(MAKE, Any_String)(level_);
            }

            c = *bp;
        }
        else {
            trap (
              c = Back_Scan_Utf8_Char(&bp, &size)
            );
            --size;  // must decrement *after* (or Back_Scan() will fail)
            if (size != 0) {
                Copy_Cell(ARG(TYPE), Datatype_From_Type(TYPE_RUNE));
                return GENERIC_CFUNC(MAKE, Any_String)(level_);
            }
        }
        trap (
          Init_Single_Codepoint_Rune(OUT, c)
        );
        return OUT; }

      default:
        break;
    }

  bad_make:

    return fail (Error_Bad_Make(heart, arg));
}


//
//  make-char: native [
//
//  "RUNE! from INTEGER! codepoint, see also TO RUNE! (to rune! 1 -> #1)"
//
//      return: [
//          char?  "(make-char 65 -> #A) (make-char 49 -> #1)"
//          NUL?   "(make char! 0 -> #{00})"
//      ]
//      codepoint [integer!]
//  ]
//
DECLARE_NATIVE(MAKE_CHAR)  // Note: currently synonym for (NUL + codepoint)
//
// This was once called CODEPOINT-TO-CHAR, which is more explicit, but not
// in the spirit of brevity of the original Rebol (make char! 65 -> #"A").
//
// !!! It seems that MAKE RUNE! could be used and having it interpet as
// codepoints wouldn't be a problem, because the visual interpretation of
// integer is covered by TO.
{
    INCLUDE_PARAMS_OF_MAKE_CHAR;

    uint32_t c = VAL_UINT32(ARG(CODEPOINT));

    if (c == 0)
        return COPY(LIB(NUL));

    trap (
      Init_Single_Codepoint_Rune(OUT, c)
    );
    return OUT;
}


//
//  to-char: native [
//
//  "Character representation, see also MAKE-CHAR"
//
//      return: [
//          char? "(to-char 1 -> #1)"
//          NUL?  "Will be #{00} NUL BLOB! representation if input is #{00}"
//      ]
//      value [char? any-utf8? blob!]
//  ]
//
DECLARE_NATIVE(TO_CHAR)
//
// !!! For efficiency, this avoids things like (to-char [A] -> #A).
// It could be that this was implemented in terms of TO RUNE! and then got
// the result and ensured it was a single character, or that the code was
// factored in such a way to permit it.  Review if real-world needs come up.
//
// !!! Because it's written this way it has redundancy with CODEPOINT OF which
// splits its implementation across generics.  Review that as well.)
//
// Note: Because CHAR? always fits in a cell (unless it's the #{00} blob which
// is locked and global), there's no point to AS-CHAR, since no series nodes
// will ever be synthesized for the result.
//
// !!! This could probably be TO RUNE! but it would be too broad, e.g. you
// wouldn't be guaranteed it was a single character.  Perhaps TO-CHAR could
// just be TO RUNE! with an ERROR! raised if it generated more than one?
{
    INCLUDE_PARAMS_OF_TO_CHAR;

    Element* e = Element_ARG(VALUE);
    if (Is_Integer(e)) {
        uint32_t c = VAL_UINT32(e);
        trap (
          Init_Single_Codepoint_Rune(OUT, c)
        );
        return OUT;
    }
    if (Is_Rune_And_Is_Char(e))
        return COPY(e);

    Size size;
    const Byte* at = Cell_Bytes_At(&size, e);
    if (size == 1) {
        if (*at == 0) {
            assert(Is_Blob(e));
            return COPY(LIB(NUL));
        }
    }

    if (size == 0)
        return fail (Error_Not_One_Codepoint_Raw());

    Codepoint c;
    const Byte* bp = at;
    if (Is_Blob(e)) {
        trap (
          c = Back_Scan_Utf8_Char(&bp, nullptr)
        );
    } else {
        bp = Back_Scan_Utf8_Char_Unchecked(&c, bp);
    }
    ++bp;
    if (bp != at + size)
        return fail (Error_Not_One_Codepoint_Raw());

    return Init_Char_Unchecked(OUT, c);  // scan checked it
}


//
//  NUL?: native [
//
//  "Test if a value is the #{00} binary BLOB!, representing codepoint 0"
//
//      return: [logic?]
//      value [element?]
//  ]
//
DECLARE_NATIVE(NUL_Q)
{
    INCLUDE_PARAMS_OF_NUL_Q;

    Element* e = Element_ARG(VALUE);
    return LOGIC(Is_Blob_And_Is_Zero(e));
}


// !!! At one time, it was allowed to do things like add #"A" to #"A".  Ren-C
// limits math operations on character to only work with numeric types (and
// will probably limit it to INTEGER! only).
//
static Result(REBI64) Get_Math_Arg_For_Char(
    Value* arg,
    const Symbol* verb
){
    switch (opt Type_Of(arg)) {
      case TYPE_INTEGER:
        return VAL_INT32(arg);

      case TYPE_DECIMAL:
        return cast(REBINT, VAL_DECIMAL(arg));

      default:
        return fail (Error_Math_Args(TYPE_RUNE, verb));
    }
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Rune)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    if (form) {
        Append_Any_Utf8_Limit(mo->strand, v, UNLIMITED);
        return TRIPWIRE;
    }

    Length len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, v);

  handle_single_char_representations: { //////////////////////////////////////

    // 1. Much reflection led to conclude that _ is the best representation
    //    for the space rune:
    //
    //      https://rebol.metaeducation.com/t/2287
    //
    // 2. There's an open question if the same issues that drive the choice
    //    of `_` to represent a literal space rune! should also apply to
    //    using `#` to represent a literal newline rune!.  In that case, the
    //    literal for a hash mark would be `##` instead of `#`.

    if (not Is_Rune_And_Is_Char(v))
        goto handle_ordinary_runes;

    Codepoint c = Rune_Known_Single_Codepoint(v);
    if (c == ' ') {
        Append_Codepoint(mo->strand, '_');  // literal can't be `# ` [1]
        goto finished;
    }
    if (c == '#') {  // might '\n' be rendered as `#`? [2]
        Append_Codepoint(mo->strand, '#');  // vs. `##`
        goto finished;
    }

} handle_ordinary_runes: { ///////////////////////////////////////////////////

    Append_Codepoint(mo->strand, '#');

    bool no_dittos = true;
    Codepoint c = Codepoint_At(cp);

    // !!! This should be smarter and share code with FILE! on whether
    // it's necessary to use double quotes or braces, and how escaping
    // should be done.  For now, just do a simple scan to get the gist
    // of what that logic *should* do.

    for (; c != '\0'; cp = Utf8_Next(&c, cp)) {
        if (c > UINT8_MAX)
            continue;
        if (
            c <= 32  // control codes up to 32 (space)
            or (
                c >= 127  // 127 is delete, begins more control codes
                and c <= 160  // 160 is non-breaking space, 161 starts Latin1
            )
        ){
            no_dittos = false;
            break;
        }
        if (Is_Lex_Delimit(cast(Byte, c))) {
            no_dittos = false;  // comma, bracket, parentheses, dittos...
            break;
        }
    }

    if (no_dittos or not Stringlike_Has_Stub(v)) {  // !!! hack
        if (len == 1 and not no_dittos) {  // use historical CHAR! molding
            bool parened = true;  // !!! used to depend on MOLD's :ALL flag

            Append_Codepoint(mo->strand, '"');
            Mold_Codepoint(mo, Rune_Known_Single_Codepoint(v), parened);
            Append_Codepoint(mo->strand, '"');
        }
        else
            Append_Any_Utf8_Limit(mo->strand, v, &len);
    }
    else {
        const Strand* s = Cell_Strand(v);  // !!! needs node
        Mold_Text_Flex_At(mo, s, 0);
    }

} finished: { ///////////////////////////////////////////////////////////////

    return TRIPWIRE;  // MOLDIFY should return TRIPWIRE
}}


IMPLEMENT_GENERIC(OLDGENERIC, Any_Utf8)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* rune = cast(Element*, ARG_N(1));
    assert(Any_Utf8(rune) and not Any_Word(rune));
    possibly(Any_String(rune));  // gets priority, but may delegate

    if (Stringlike_Has_Stub(rune)) {
        assert(not Is_Rune_And_Is_Char(rune));  // no string math
        return GENERIC_CFUNC(OLDGENERIC, Any_String)(level_);
    }

    // !!! All the math operations below are inherited from the CHAR!
    // implementation, and will not work if the RUNE! length is > 1.
    //
    if (not Is_Rune_And_Is_Char(rune))
        panic ("Math operations only usable on single-character RUNE!");

    require (
      Codepoint c = Get_Rune_Single_Codepoint(rune)
    );

    // Don't use a Codepoint for chr, because it does signed math and then will
    // detect overflow.
    //
    REBI64 chr = cast(REBI64, c);
    REBI64 arg;

    switch (opt id) {
      case SYM_ADD: {
        require (
          arg = Get_Math_Arg_For_Char(ARG_N(2), verb)
        );
        chr += arg;
        break; }

      case SYM_SUBTRACT: {
        require (
          arg = Get_Math_Arg_For_Char(ARG_N(2), verb)
        );
        chr -= arg;
        break; }

      default:
        panic (UNHANDLED);
    }

    if (chr < 0)
        return fail (Error_Codepoint_Negative_Raw());

    trap (
      Init_Single_Codepoint_Rune(OUT, cast(Codepoint, chr))
    );
    return OUT;
}


// TO conversions for ANY-UTF8? types are a superset of the concerns for
// ANY-STRING? and ANY-WORD? types (which always have a Stub allocation,
// instead of just sometimes).  So strings and words are delegated here.
//
// 1. While the limits are still shaping up, it's believed that:
//
//       >> to block! "a 1 <b>"
//       == [a 1 <b>]
//
//    This would be a limited form of transcoding that would not allow
//    comments, and may be limited in some ways regarding spacing as
//    well (the requirements of matching reverse transformations would
//    have to be relaxed if spaces were thrown out).
//
// 2. If we know something about the string we may be able to avoid
//    running a transcode, e.g.:
//
//        >> str: as text! 'some-word  ; string node has symbol "flavor"
//
//        >> to fence! str
//        == {some-word}  ; can beeline here for symbol-flavor strings
//
//    This optimization may not be particularly important, but it points
//    to a potential family of such optimizations.
//
IMPLEMENT_GENERIC(TO, Any_Utf8)
{
    INCLUDE_PARAMS_OF_TO;

    Element* v = Element_ARG(VALUE);  // rune, email, etc.
    Heart to = Datatype_Builtin_Heart(ARG(TYPE));
    possibly(Any_Word(v));  // delegates some cases

    if (Any_String_Type(to)) {  // always need mutable new copy of data
        Length len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, v);
        require (
          Strand* s = Make_Strand(size)
        );
        memcpy(cast(Byte*, Strand_Head(s)), cast(Byte*, utf8), size);
        Term_Strand_Len_Size(s, len, size);
        return Init_Any_String(OUT, to, s);
    }

    if (to == TYPE_WORD) {
        assert(not Any_Word(v));  // does not delegate this case
        if (not Any_String(v) or Is_Flex_Frozen(Cell_Strand(v)))
            return GENERIC_CFUNC(AS, Any_Utf8)(LEVEL);  // immutable src

        Size size;  // TO conversion of mutable data, can't reuse stub
        Utf8(const*) at = Cell_Utf8_Size_At(&size, v);
        require (
          const Symbol* sym = Intern_Utf8_Managed(at, size)
        );
        return Init_Word(OUT, sym);
    }

    if (to == TYPE_RUNE or to == TYPE_MONEY) {  // may make node if mutable
        if (not Any_String(v) or Is_Flex_Frozen(Cell_Strand(v))) {
            possibly(Any_Word(v));
            return GENERIC_CFUNC(AS, Any_Utf8)(LEVEL);  // immutable src
        }

        Length len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, v);
        return Init_Utf8_Non_String(  // may fit utf8 in cell if small
            OUT, to, utf8, size, len
        );
    }

    if (to == TYPE_EMAIL or to == TYPE_URL) {
        Length len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, v);

        if (to == TYPE_EMAIL) {
            require (
              const Byte* ep = Scan_Email_To_Stack(utf8, size)
            );
            if (ep != cast(const Byte*, utf8) + size)
                return fail (Error_Scan_Invalid_Raw(ARG(TYPE), v));

            Move_Cell(OUT, TOP_ELEMENT);
            DROP();
            return OUT;
        }

        if (
            cast(const Byte*, utf8) + size
            != Try_Scan_URL_To_Stack(utf8, size)
        ){
            return fail (Error_Scan_Invalid_Raw(ARG(TYPE), v));
        }
        Move_Cell(OUT, TOP_ELEMENT);
        DROP();
        return OUT;
    }

    if (
        to == TYPE_INTEGER
        or to == TYPE_DECIMAL
        or to == TYPE_PERCENT
        or to == TYPE_DATE
        or to == TYPE_TIME
        or to == TYPE_PAIR
    ){
        trap (
          Transcode_One(OUT, to, v)
        );
        return OUT;
    }

    if (Any_Sequence_Type(to)) {  // to tuple! "a.b.c" -> a.b.c
        trap (
          Transcode_One(OUT, to, v)
        );
        return OUT;
    }

    if (Any_List_Type(to)) {  // limited TRANSCODE (how limited?...) [1]
        if (Stringlike_Has_Stub(v)) {
            if (Stub_Flavor(Cell_Strand(v)) == FLAVOR_SYMBOL)  // [2]
                return rebValue(CANON(ENVELOP), rebQ(ARG(TYPE)), rebQ(v));
        }
        return rebValue(CANON(AS), rebQ(ARG(TYPE)), CANON(TRANSCODE), rebQ(v));
    }

    panic (UNHANDLED);
}


//
//  Alias_Any_Utf8_As: C
//
// 1. If the payload of non-string UTF-8 value lives in the Cell itself,
//    a read-only Flex must be created for the data...because otherwise
//    there isn't room for an index (which ANY-STRING? needs).  For
//    behavior parity with if the payload *was* in the Cell, this alias
//    must be frozen.
//
// 2. We don't want to expose the implementation detail of where the byte
//    count crossover is that an in-cell UTF-8 compression happens, so
//    if we create a node we have to give it the same constraints that
//    would apply if we had reused one.
//
Result(Element*) Alias_Any_Utf8_As(
    Sink(Element) out,
    const Element* v,
    Heart as
){
    assert(not Any_Word(v));  // not delegated

    if (Any_String_Type(as)) {  // have to create a Flex if not stub [1]
        assert(not Any_String(v));  // not delegated by string generic
        if (Stringlike_Has_Stub(v)) {
            possibly(Is_Flex_Frozen(Cell_Strand(v)));
            possibly(Is_Stub_Symbol(Cell_Strand(v)));
            Copy_Cell(out, v);
            KIND_BYTE(out) = as;
            return out;
        }

    make_small_utf8_at_index_0: { //////////////////////////////////////

        REBLEN len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, v);
        assert(size + 1 <= Size_Of(v->payload.at_least_8));

        require (
          Strand* str = Make_Strand_Core(
            STUB_MASK_STRAND | BASE_FLAG_MANAGED,
            size
        ));
        memcpy(
            Flex_Data(str),
            cast(Byte*, utf8),
            size + 1  // +1 to include '\0'
        );
        Term_Strand_Len_Size(str, len, size);
        Freeze_Flex(str);
        possibly(as == TYPE_BLOB);  // index 0 so byte transform not needed
        return Init_Series(out, as, str);
    }}

    if (as == TYPE_WORD) {  // aliasing as WORD! freezes data
        if (Stringlike_Has_Stub(v)) {
            const Strand* str = Cell_Strand(v);
            if (Series_Index(v) != 0)
                return fail ("Can't alias string as WORD! unless at head");

            if (Is_Strand_Symbol(str))  // already frozen and checked!
                return Init_Word(out, cast(const Symbol*, str));

            if (not Is_Flex_Frozen(str)) {  // always force frozen
                if (Get_Cell_Flag(v, CONST))
                    return fail (Error_Alias_Constrains_Raw());
                Freeze_Flex(str);
            }
        }

        // !!! Logic to re-use Stub if newly interned symbol not written

        Size size;
        Utf8(const*) at = Cell_Utf8_Size_At(&size, v);
        trap (
          const Symbol* sym = Intern_Utf8_Managed(at, size)
        );
        return Init_Word(out, sym);
    }

    if (as == TYPE_BLOB) {  // resulting binary is UTF-8 constrained [2]
        if (Stringlike_Has_Stub(v))
            return Init_Blob_At(
                out,
                Cell_Strand(v),
                String_Byte_Offset_At(v)  // index has to be in terms of bytes
            );

        goto make_small_utf8_at_index_0;
    }

    if (as == TYPE_INTEGER) {
        trap (
          Codepoint c = Get_Rune_Single_Codepoint(v)
        );
        return Init_Integer(out, c);
    }

    if (as == TYPE_RUNE or as == TYPE_MONEY) {  // fits cell or freeze string
        assert(as != TYPE_WORD and not (Any_String_Type(as)));

        if (Stringlike_Has_Stub(v)) {
            const Strand *s = Cell_Strand(v);
            if (not Is_Flex_Frozen(s)) {  // always force frozen
                if (Get_Cell_Flag(v, CONST))
                    return fail (Error_Alias_Constrains_Raw());
                Freeze_Flex(s);
            }
        }

        Length len;
        Size size = String_Size_Limit_At(&len, v, UNLIMITED);

        if (Try_Init_Small_Utf8(out, as, String_At(v), len, size))
            return out;

        Copy_Cell(out, v);  // index heeded internally, not exposed
        KIND_BYTE(out) = as;
        return out;
    }

    if (as == TYPE_EMAIL or as == TYPE_URL) {
        if (Stringlike_Has_Stub(v)) {
            const Strand *s = Cell_Strand(v);
            if (not Is_Flex_Frozen(s)) {  // always force frozen
                if (Get_Cell_Flag(v, CONST))
                    return fail (Error_Alias_Constrains_Raw());
                Freeze_Flex(s);
            }
        }
        // have to validate the email or URL.  Build on top of logic in
        // the TO routine to do that, even though it copies the String.
        //
        const Value* datatype_as = Datatype_From_Type(as);
        Value* result = rebValue(CANON(TO), rebQ(datatype_as), rebQ(v));
        Copy_Cell(out, Known_Element(result));
        rebRelease(result);
        return out;
    }

    return fail (Error_Invalid_Type(as));
}


IMPLEMENT_GENERIC(AS, Any_Utf8)
{
    INCLUDE_PARAMS_OF_AS;

    Element* any_utf8 = Element_ARG(VALUE);
    Heart as = Datatype_Builtin_Heart(ARG(TYPE));

    require (
      Alias_Any_Utf8_As(OUT, any_utf8, as)
    );
    return OUT;
}


// !!! Review if RUNE! should be PICK-able at all, or if you should have to
// alias it as TEXT!... this would go along with the idea of saying that
// the (_) "Space Rune" is EMPTY?.
//
IMPLEMENT_GENERIC(TWEAK_P, Is_Rune)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    const Element* rune = Element_ARG(LOCATION);
    const Value* picker = Element_ARG(PICKER);

    if (not Is_Integer(picker))
        panic (PARAM(PICKER));

    REBI64 n = VAL_INT64(picker);

    Value* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    if (n <= 0)
        return DUAL_SIGNAL_NULL_ABSENT;

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, rune);
    if (n > len)
        return DUAL_SIGNAL_NULL_ABSENT;

    Codepoint c;
    cp = Utf8_Next(&c, cp);
    for (; n != 1; --n)
        cp = Utf8_Next(&c, cp);

    return DUAL_LIFTED(Init_Char_Unchecked(OUT, c));

} handle_poke: { /////////////////////////////////////////////////////////////

    panic ("RUNE! is immutable, cannot be modified");
}}


IMPLEMENT_GENERIC(REVERSE_OF, Any_Utf8)
{
    INCLUDE_PARAMS_OF_REVERSE_OF;

    Element* any_utf8 = Element_ARG(VALUE);
    Value* part = ARG(PART);

    Value* datatype = Copy_Cell(SPARE, Datatype_Of(any_utf8));

    return Delegate_Operation_With_Part(
        SYM_REVERSE, SYM_TEXT_X,
        Liftify(datatype), Quotify(any_utf8), Liftify(part)
    );
}


// !!! This is how R3-Alpha randomized based on strings.  Is it good?
//
IMPLEMENT_GENERIC(RANDOMIZE, Any_Utf8)
{
    INCLUDE_PARAMS_OF_RANDOMIZE;

    Element* any_utf8 = Element_ARG(SEED);

    Size utf8_size;
    Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, any_utf8);
    Set_Random(crc32_z(0L, utf8, utf8_size));
    return TRIPWIRE;
}


IMPLEMENT_GENERIC(RANDOM, Is_Rune)
{
    INCLUDE_PARAMS_OF_RANDOM;

    Element* rune = Element_ARG(MAX);

    require (
      Codepoint limit = Get_Rune_Single_Codepoint(rune)
    );

  keep_generating_until_valid_char_found: {

  // RUNE! doesn't allow you to create unicode codepoints with surrogate
  // values or other illegal states, including 0.  All bad states should give
  // back an error.

    attempt {
        Codepoint c = cast(Codepoint,
            1 + (Random_Int(Bool_ARG(SECURE)) % limit)
        );

        Init_Single_Codepoint_Rune(OUT, c) except (Error* e) {
            dont(Free_Unmanaged_Flex(Varlist_Array(e)));  // errors prealloc'd
            UNUSED(e);
            again;
        }
    }

    return OUT;
}}


IMPLEMENT_GENERIC(SHUFFLE_OF, Any_Utf8)
{
    INCLUDE_PARAMS_OF_SHUFFLE_OF;

    Element* any_utf8 = Element_ARG(VALUE);
    Value* part = ARG(PART);

    if (Bool_ARG(SECURE))
        panic (Error_Bad_Refines_Raw());

    Value* datatype = Copy_Cell(SPARE, Datatype_Of(any_utf8));

    return Delegate_Operation_With_Part(
        SYM_SHUFFLE, SYM_TEXT_X,
        Liftify(datatype), Quotify(any_utf8), Liftify(part)
    );
}


//
//  codepoint-of: native:generic [
//
//  "Get the singular codepoint that an RUNE! or BINARY! correspond to"
//
//      return: [<null> integer!]
//      value [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(CODEPOINT_OF)
{
    INCLUDE_PARAMS_OF_CODEPOINT_OF;

    return Dispatch_Generic(CODEPOINT_OF, Element_ARG(VALUE), LEVEL);
}


IMPLEMENT_GENERIC(CODEPOINT_OF, Is_Rune)
{
    INCLUDE_PARAMS_OF_CODEPOINT_OF;

    Element* rune = Element_ARG(VALUE);

    Option(Codepoint) c = Codepoint_Of_Rune_If_Single_Char(rune);

    if (not c)
        return fail (Error_Not_One_Codepoint_Raw());

    return Init_Integer(OUT, unwrap c);
}


IMPLEMENT_GENERIC(LENGTH_OF, Any_Utf8)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* v = Element_ARG(VALUE);
    possibly(Any_Word(v));  // !!! should WORD! disallow LENGTH OF ?

    REBLEN len;
    Cell_Utf8_Len_Size_At(&len, nullptr, v);
    return Init_Integer(OUT, len);
}


IMPLEMENT_GENERIC(SIZE_OF, Any_Utf8)
{
    INCLUDE_PARAMS_OF_SIZE_OF;

    Element* v = Element_ARG(VALUE);
    possibly(Any_String(v));  // delegates here
    possibly(Any_Word(v));  // !!! should WORD! disable `size of`?

    Size size;
    Cell_Utf8_Size_At(&size, v);
    return Init_Integer(OUT, size);
}


//
//  trailing-bytes-for-utf8: native [
//
//  "Given the first byte of a UTF-8 encoding, how many bytes should follow"
//
//      return: [integer!]
//      first-byte [integer!]
//      :extended "Permit 4 or 5 trailing bytes, not legal in the UTF-8 spec"
//  ]
//
DECLARE_NATIVE(TRAILING_BYTES_FOR_UTF8)
//
// !!! This is knowledge Rebol has, and it can be useful for anyone writing
// code that processes UTF-8 (e.g. the terminal).  Might as well expose it.
{
    INCLUDE_PARAMS_OF_TRAILING_BYTES_FOR_UTF8;

    REBINT byte = VAL_INT32(ARG(FIRST_BYTE));
    if (byte < 0 or byte > 255)
        panic (Error_Out_Of_Range(ARG(FIRST_BYTE)));

    uint_fast8_t trail = g_trailing_bytes_for_utf8[cast(Byte, byte)];
    if (trail > 3 and not Bool_ARG(EXTENDED)) {
        assert(trail == 4 or trail == 5);
        panic (
            "Use :EXTENDED with TRAILING-BYTES-FOR-UTF-8 for 4 or 5 bytes"
        );
    }

    return Init_Integer(OUT, trail);
}
