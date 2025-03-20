//
//  File: %t-char.c
//  Summary: "character datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//  Trap_Back_Scan_Utf8_Char: C
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
//         if (*bp < 0x80) {
//             // do ASCII stuff...
//         }
//         else {
//             Codepoint uni;
//             Option(Error*) e = Trap_Back_Scan_Utf8_Char(&uni, &bp, &size);
//             if (e) { /* handle error */ }
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
//      https://github.com/rebol/rebol-issues/issues/638
//      https://en.wikipedia.org/wiki/UTF-8#Overlong_encodings
//
//    ...which meant that various illegal input patterns would be tolerated,
//    so long as they didn't cause crashes.  You would just not have the
//    input validated, and get garbage characters out.  The Ren-C philosophy
//    is that since this check only applies to non-ASCII, it is worth it to
//    do the validation.  And it only applies when scanning strings...once
//    they are loaded into String* we use Back_Scan_Utf8_Char_Unchecked().
//
// 3. We want the erroring cases to be inexpensive, because UTF-8 characters
//    are scanned for instance in FIND of a TEXT! in a binary BLOB! which may
//    have lots of invalid UTF-8.  So all the errors used here are
//    pre-allocated.  But those allocations only happen once the error
//    machinery is ready.
//
Option(Error*) Trap_Back_Scan_Utf8_Char(
    Sink(Codepoint) out,  // valid codepoint, no NUL or substitution chars [1]
    const Byte** bp,  // left alone if error result, "back updated" if not
    Option(Need(Size*)) size  // decremented in non-error case
){
    Codepoint c = 0;

    const Byte* source = *bp;
    uint_fast8_t trail = g_trailing_bytes_for_utf8[*source];

    if (size) {  // Check that we have enough valid source bytes
        if (cast(uint_fast8_t, trail + 1) > *(unwrap size))
            return Cell_Error(g_error_utf8_too_short);  // cached [3]
    }
    else if (trail != 0) {
        do {
            if (source[trail] < 0x80)
                return Cell_Error(g_error_utf8_trail_bad_bit);  // cached [3]
        } while (--trail != 0);

        trail = g_trailing_bytes_for_utf8[*source];
    }

    if (not Is_Legal_UTF8(source, trail + 1))  // was omitted in R3-Alpha [2]
        return Cell_Error(g_error_overlong_utf8);  // cached [3]

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
        return Cell_Error(g_error_codepoint_too_high);  // cached [3]
    if (c >= UNI_SUR_HIGH_START and c <= UNI_SUR_LOW_END)
        return Cell_Error(g_error_no_utf8_surrogates);  // cached [3]

    if (c == 0)  // string types disallow internal 0 bytes in Ren-C [1]
        return Cell_Error(g_error_illegal_zero_byte);  // cached [3]

    if (size)
        *(unwrap size) -= trail;

    *out = c;
    *bp += trail;
    return nullptr;  // no error to return, success!
}


//
//  CT_Utf8: C
//
// 1. As the replacement for CHAR!, ISSUE! inherits the behavior that there
//    are no non-strict comparisons.  To compare non-strictly, they must be
//    aliased as TEXT!.  (!!! This should be reviewed.)
//
REBINT CT_Utf8(const Cell* a, const Cell* b, bool strict)
{
    assert(Any_Utf8_Type(Cell_Heart(a)));
    assert(Any_Utf8_Type(Cell_Heart(b)));

    if (Cell_Heart(a) == TYPE_ISSUE or Cell_Heart(b) == TYPE_ISSUE)
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

    return LOGIC(CT_Utf8(ARG(VALUE1), ARG(VALUE2), REF(STRICT)) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Any_Utf8)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    return LOGIC(CT_Utf8(ARG(VALUE1), ARG(VALUE2), true) == -1);
}


IMPLEMENT_GENERIC(MAKE, Any_Utf8)
{
    INCLUDE_PARAMS_OF_MAKE;

    Heart heart = Cell_Datatype_Heart(ARG(TYPE));
    assert(Any_Utf8_Type(heart));

    Element* arg = Element_ARG(DEF);

    switch(Type_Of(arg)) {
      case TYPE_INTEGER: {
        if (heart != TYPE_ISSUE)
            fail ("Only ISSUE! can MAKE a UTF-8 immutable type with INTEGER!");

        REBINT n = Int32(arg);
        Option(Error*) error = Trap_Init_Char(OUT, n);
        if (error)
            return RAISE(unwrap error);
        return OUT; }

      case TYPE_BLOB: {
        if (heart != TYPE_ISSUE)
            fail ("Only ISSUE! can MAKE a UTF-8 immutable type with BLOB!");

        Size size;
        const Byte* bp = Cell_Blob_Size_At(&size, arg);
        if (size == 0)
            goto bad_make;

        Codepoint c;
        if (*bp <= 0x80) {
            if (size != 1) {
                Init_Builtin_Datatype(ARG(TYPE), TYPE_ISSUE);
                return GENERIC_CFUNC(MAKE, Any_String)(level_);
            }

            c = *bp;
        }
        else {
            Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c, &bp, &size);
            if (e)
                return RAISE(unwrap e);  // must be valid UTF8

            --size;  // must decrement *after* (or Back_Scan() will fail)
            if (size != 0) {
                Init_Builtin_Datatype(ARG(TYPE), TYPE_ISSUE);
                return GENERIC_CFUNC(MAKE, Any_String)(level_);
            }
        }
        Option(Error*) error = Trap_Init_Char(OUT, c);
        if (error)
            return RAISE(unwrap error);
        return OUT; }

      default:
        break;
    }

  bad_make:

    return RAISE(Error_Bad_Make(heart, arg));
}


//
//  /make-char: native [
//
//  "Codepoint from integer, e.g. make-char 65 -> #A (see also TO-CHAR)"
//
//      return: "Can also be NUL as binary BLOB!, make char! 0 -> #{00}"
//          [char?]
//      codepoint [integer!]
//  ]
//
DECLARE_NATIVE(MAKE_CHAR)  // Note: currently synonym for (NUL + codepoint)
//
// Note: Consideration was given to (make-char [1 + 2] -> #3) as a way to
// get an assured single-character result from a mold.  (to-char mold 1 + 2)
// does the same thing, so it's probably not necessary.
//
// This was once called CODEPOINT-TO-CHAR, which is more explicit, but not
// in the spirit of brevity of the original Rebol (make char! 65 -> #"A").
// It's nice to have Ren-C be strictly better, as (make-char 65 -> #A)
{
    INCLUDE_PARAMS_OF_MAKE_CHAR;

    uint32_t c = VAL_UINT32(ARG(CODEPOINT));
    Option(Error*) error = Trap_Init_Char(OUT, c);
    if (error)
        return RAISE(unwrap error);
    return OUT;
}


//
//  /to-char: native [
//
//  "Character representation, e.g. to-char 1 -> #1 (see also MAKE-CHAR)"
//
//      return: "Will be #{00} NUL BLOB! representation if input is #{00}"
//          [char?]
//      element [char? any-utf8? blob!]
//  ]
//
DECLARE_NATIVE(TO_CHAR)
//
// !!! For efficiency, this avoids things like (to-char [A] -> #A).
// It could be that this was implemented in terms of TO ISSUE! and then got
// the result and ensured it was a single character, or that the code was
// factored in such a way to permit it.  Review if real-world needs come up.
//
// !!! Because it's written this way it has redundancy with CODEPOINT OF which
// splits its implementation across generics.  Review that as well.)
//
// Note: Because CHAR? always fits in a cell (unless it's the #{00} blob which
// is locked and global), there's no point to AS-CHAR, since no series nodes
// will ever be synthesized for the result.
{
    INCLUDE_PARAMS_OF_TO_CHAR;

    Element* e = Element_ARG(ELEMENT);
    if (Is_Integer(e)) {
        uint32_t c = VAL_UINT32(e);
        Option(Error*) error = Trap_Init_Char(OUT, c);
        if (error)
            return RAISE(unwrap error);
        return OUT;
    }
    if (IS_CHAR(e))
        return COPY(e);

    Size size;
    const Byte* at = Cell_Bytes_At(&size, e);
    if (size == 1) {
        if (*at == 0) {
            assert(Is_Blob(e));
            return COPY(LIB(NUL));
        }
    }
    Codepoint c;
    const Byte* bp = at;
    if (size == 0)
        return RAISE(Error_Not_One_Codepoint_Raw());
    if (Is_Blob(e)) {
        Option(Error*) error = Trap_Back_Scan_Utf8_Char(&c, &bp, nullptr);
        if (error)
            return RAISE(unwrap error);
    } else {
        bp = Back_Scan_Utf8_Char_Unchecked(&c, bp);
    }
    ++bp;
    if (bp != at + size)
        return RAISE(Error_Not_One_Codepoint_Raw());
    return Init_Char_Unchecked(OUT, c);  // scan checked it
}


//
//  /NUL?: native [
//
//  "Test if a value is the #{00} binary BLOB!, representing codepoint 0"
//
//      return: [logic?]
//      element [element?]
//  ]
//
DECLARE_NATIVE(NUL_Q)
{
    INCLUDE_PARAMS_OF_NUL_Q;

    Element* e = Element_ARG(ELEMENT);
    return Init_Logic(OUT, Is_NUL(e));
}


static REBINT Math_Arg_For_Char(Value* arg, const Symbol* verb)
{
    switch (Type_Of(arg)) {
      case TYPE_ISSUE:
        return Cell_Codepoint(arg);

      case TYPE_INTEGER:
        return VAL_INT32(arg);

      case TYPE_DECIMAL:
        return cast(REBINT, VAL_DECIMAL(arg));

      default:
        fail (Error_Math_Args(TYPE_ISSUE, verb));
    }
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Sigil)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = REF(FORM);

    UNUSED(form);
    Append_Any_Utf8(mo->string, v);

    return NOTHING;
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Issue)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = REF(FORM);

    if (form) {
        if (IS_CHAR_CELL(v) and Cell_Codepoint(v) == 0)
            fail (Error_Illegal_Zero_Byte_Raw());  // don't form #, only mold

        Append_Any_Utf8_Limit(mo->string, v, UNLIMITED);
        return NOTHING;
    }

    Length len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, v);

    Append_Codepoint(mo->string, '#');

    if (len == 0) {
        Append_Codepoint(mo->string, '"');
        Append_Codepoint(mo->string, '"');
        return NOTHING;
    }

    bool no_quotes = true;
    Codepoint c = Codepoint_At(cp);

    if (len == 1 and c == ' ')
        return NOTHING;  // # is notationally a space character

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
            no_quotes = false;
            break;
        }
        if (Is_Lex_Delimit(cast(Byte, c))) {
            no_quotes = false;  // comma, bracket, parentheses, quotes...
            break;
        }
    }

    if (no_quotes or not Stringlike_Has_Node(v)) {  // !!! hack
        if (len == 1 and not no_quotes) {  // use historical CHAR! molding
            bool parened = true;  // !!! used to depend on MOLD's :ALL flag

            Append_Codepoint(mo->string, '"');
            Mold_Codepoint(mo, Cell_Codepoint(v), parened);
            Append_Codepoint(mo->string, '"');
        }
        else
            Append_Any_Utf8_Limit(mo->string, v, &len);
    }
    else {
        const String* s = Cell_String(v);  // !!! needs node
        Mold_Text_Flex_At(mo, s, 0);
    }

    return NOTHING;
}


IMPLEMENT_GENERIC(OLDGENERIC, Any_Utf8)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* issue = cast(Element*, ARG_N(1));
    assert(Any_Utf8(issue) and not Any_Word(issue));
    possibly(Any_String(issue));  // gets priority, but may delegate

    if (Stringlike_Has_Node(issue)) {
        assert(not IS_CHAR(issue));  // no string math
        return GENERIC_CFUNC(OLDGENERIC, Any_String)(level_);
    }

    // !!! All the math operations below are inherited from the CHAR!
    // implementation, and will not work if the ISSUE! length is > 1.
    //
    if (not IS_CHAR(issue))
        return FAIL("Math operations only usable on single-character ISSUE!");

    // Don't use a Codepoint for chr, because it does signed math and then will
    // detect overflow.
    //
    REBI64 chr = cast(REBI64, Cell_Codepoint(issue));
    REBI64 arg;

    switch (id) {
      case SYM_ADD: {
        arg = Math_Arg_For_Char(ARG_N(2), verb);
        chr += arg;
        break; }

      case SYM_SUBTRACT: {
        arg = Math_Arg_For_Char(ARG_N(2), verb);

        // Rebol2 and Red return CHAR! values for subtraction from another
        // CHAR! (though Red checks for overflow and errors on something like
        // `subtract #"^(00)" #"^(01)"`, vs returning #"^(FF)").
        //
        // R3-Alpha chose to return INTEGER! and gave a signed difference, so
        // the above would give -1.
        //
        if (IS_CHAR(ARG_N(2))) {
            Init_Integer(OUT, chr - arg);
            return OUT;
        }

        chr -= arg;
        break; }

      case SYM_DIVIDE:
        arg = Math_Arg_For_Char(ARG_N(2), verb);
        if (arg == 0)
            return FAIL(Error_Zero_Divide_Raw());
        chr /= arg;
        break;

      case SYM_REMAINDER:
        arg = Math_Arg_For_Char(ARG_N(2), verb);
        if (arg == 0)
            return FAIL(Error_Zero_Divide_Raw());
        chr %= arg;
        break;

      case SYM_BITWISE_NOT:
        chr = cast(Codepoint, ~chr);
        break;

      case SYM_BITWISE_AND:
        arg = Math_Arg_For_Char(ARG_N(2), verb);
        chr &= cast(Codepoint, arg);
        break;

      case SYM_BITWISE_OR:
        arg = Math_Arg_For_Char(ARG_N(2), verb);
        chr |= cast(Codepoint, arg);
        break;

      case SYM_BITWISE_XOR:
        arg = Math_Arg_For_Char(ARG_N(2), verb);
        chr ^= cast(Codepoint, arg);
        break;

      case SYM_BITWISE_AND_NOT:
        arg = Math_Arg_For_Char(ARG_N(2), verb);
        chr &= cast(Codepoint, ~arg);
        break;

      case SYM_EVEN_Q:
        return Init_Logic(OUT, did (cast(Codepoint, ~chr) & 1));

      case SYM_ODD_Q:
        return Init_Logic(OUT, did (chr & 1));

      default:
        return UNHANDLED;
    }

    if (chr < 0)
        return RAISE(Error_Codepoint_Negative_Raw());

    Option(Error*) error = Trap_Init_Char(OUT, cast(Codepoint, chr));
    if (error)
        return RAISE(unwrap error);
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

    Element* v = Element_ARG(ELEMENT);  // issue, email, etc.
    Heart to = Cell_Datatype_Heart(ARG(TYPE));
    possibly(Any_Word(v));  // delegates some cases

    if (Any_String_Type(to)) {  // always need mutable new copy of data
        Length len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, v);
        String* s = Make_String(size);
        memcpy(String_Head(s), utf8, size);
        Term_String_Len_Size(s, len, size);
        return Init_Any_String(OUT, to, s);
    }

    if (Any_Word_Type(to)) {
        assert(not Any_Word(v));  // does not delegate this case
        if (not Any_String(v) or Is_Flex_Frozen(Cell_String(v)))
            return GENERIC_CFUNC(AS, Any_Utf8)(LEVEL);  // immutable src

        Size size;  // TO conversion of mutable data, can't reuse stub
        Utf8(const*) at = Cell_Utf8_Size_At(&size, v);
        const Symbol* sym = Intern_UTF8_Managed(at, size);
        return Init_Any_Word(OUT, to, sym);
    }

    if (to == TYPE_ISSUE) {  // may have to make node if source mutable
        if (not Any_String(v) or Is_Flex_Frozen(Cell_String(v))) {
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

    if (to == TYPE_EMAIL or to == TYPE_URL or to == TYPE_SIGIL) {
        Length len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, v);

        if (to == TYPE_EMAIL) {
            if (
                cast(const Byte*, utf8) + size
                != Try_Scan_Email_To_Stack(utf8, size)
            ){
                return RAISE(Error_Scan_Invalid_Raw(ARG(TYPE), v));
            }
            return Move_Drop_Top_Stack_Element(OUT);
        }

        if (to == TYPE_URL) {
            if (
                cast(const Byte*, utf8) + size
                != Try_Scan_URL_To_Stack(utf8, size)
            ){
                return RAISE(Error_Scan_Invalid_Raw(ARG(TYPE), v));
            }
            return Move_Drop_Top_Stack_Element(OUT);
        }

        assert(to == TYPE_SIGIL);  // transcoding is slow--need to refactor
        Option(Error*) error = Trap_Transcode_One(OUT, TYPE_SIGIL, v);
        if (error)
            return RAISE(unwrap error);
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
        Option(Error*) error = Trap_Transcode_One(OUT, to, v);
        if (error)
            return RAISE(unwrap error);
        return OUT;
    }

    if (Any_Sequence_Type(to)) {  // to the-tuple! "a.b.c" -> @a.b.c
        Heart plain;
        if (Any_Tuple_Type(to))
            plain = TYPE_TUPLE;
        else if (Any_Chain_Type(to))
            plain = TYPE_CHAIN;
        else {
            assert(Any_Path_Type(to));
            plain = TYPE_PATH;
        }
        Option(Error*) error = Trap_Transcode_One(OUT, plain, v);
        if (error)
            return RAISE(unwrap error);
        HEART_BYTE(OUT) = to;
        return OUT;
    }

    if (Any_List_Type(to)) {  // limited TRANSCODE (how limited?...) [1]
        if (Stringlike_Has_Node(v)) {
            if (Stub_Flavor(Cell_String(v)) == FLAVOR_SYMBOL)  // [2]
                return rebValue(CANON(ENVELOP), ARG(TYPE), rebQ(v));
        }
        return rebValue(CANON(AS), ARG(TYPE), CANON(TRANSCODE), rebQ(v));
    }

    if (to == TYPE_BLANK)
        return GENERIC_CFUNC(AS, Any_Utf8)(LEVEL);

    return UNHANDLED;
}


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
IMPLEMENT_GENERIC(AS, Any_Utf8)
{
    INCLUDE_PARAMS_OF_AS;

    Element* v = Element_ARG(ELEMENT);  // issue, email, etc.
    Heart as = Cell_Datatype_Heart(ARG(TYPE));
    assert(not Any_Word(v));  // not delegated

    if (Any_String_Type(as)) {  // have to create a Flex if not node [1]
        assert(not Any_String(v));  // not delegated by string generic
        if (Stringlike_Has_Node(v)) {
            possibly(Is_Flex_Frozen(Cell_String(v)));
            possibly(Is_Stub_Symbol(Cell_String(v)));
            Copy_Cell(OUT, v);
            HEART_BYTE(OUT) = as;
            return OUT;
        }

    make_small_utf8_at_index_0: { //////////////////////////////////////

        REBLEN len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, v);
        assert(size + 1 <= Size_Of(v->payload.at_least_8));

        String* str = Make_String_Core(FLEX_MASK_MANAGED_STRING, size);
        memcpy(Flex_Data(str), utf8, size + 1);  // +1 to include '\0'
        Term_String_Len_Size(str, len, size);
        Freeze_Flex(str);
        possibly(as == TYPE_BLOB);  // index 0 so byte transform not needed
        return Init_Series(OUT, as, str);
    }}

    if (Any_Word_Type(as)) {  // aliasing as an ANY-WORD? freezes data
        if (Stringlike_Has_Node(v)) {
            const String* str = Cell_String(v);
            if (VAL_INDEX(v) != 0)
                return FAIL("Can't alias string as WORD! unless at head");

            if (Is_String_Symbol(str))  // already frozen and checked!
                return Init_Any_Word(OUT, as, cast(const Symbol*, str));

            if (not Is_Flex_Frozen(str)) {  // always force frozen
                if (Get_Cell_Flag(v, CONST))
                    return FAIL(Error_Alias_Constrains_Raw());
                Freeze_Flex(str);
            }
        }

        // !!! Logic to re-use Stub if newly interned symbol not written

        Size size;
        Utf8(const*) at = Cell_Utf8_Size_At(&size, v);
        const Symbol* sym = Intern_UTF8_Managed(at, size);
        Init_Any_Word(OUT, as, sym);
        return OUT;
    }

    if (as == TYPE_BLOB) {  // resulting binary is UTF-8 constrained [2]
        if (Stringlike_Has_Node(v)) {
            Init_Blob_At(
                OUT,
                Cell_String(v),
                VAL_BYTEOFFSET(v)  // index has to be in terms of bytes
            );
            HEART_BYTE(OUT) = TYPE_BLOB;
            return OUT;
        }

        goto make_small_utf8_at_index_0;
    }

    if (as == TYPE_INTEGER) {
        if (not IS_CHAR(v))
        return FAIL(
            "AS INTEGER! only supports what-were-CHAR! issues ATM"
        );
        return Init_Integer(OUT, Cell_Codepoint(v));
    }

    if (as == TYPE_ISSUE) {  // try to fit in cell, or use frozen string
        assert(not Any_Word_Type(as) and not (Any_String_Type(as)));

        if (Stringlike_Has_Node(v)) {
            const String *s = Cell_String(v);
            if (not Is_Flex_Frozen(s)) {  // always force frozen
                if (Get_Cell_Flag(v, CONST))
                    return FAIL(Error_Alias_Constrains_Raw());
                Freeze_Flex(s);
            }
        }

        Length len;
        Size size = Cell_String_Size_Limit_At(&len, v, UNLIMITED);

        if (Try_Init_Small_Utf8(OUT, as, Cell_String_At(v), len, size))
            return OUT;

        Copy_Cell(OUT, v);  // index heeded internally, not exposed
        HEART_BYTE(OUT) = as;
        return OUT;
    }

    if (as == TYPE_EMAIL or as == TYPE_URL or as == TYPE_SIGIL) {
        if (Stringlike_Has_Node(v)) {
            const String *s = Cell_String(v);
            if (not Is_Flex_Frozen(s)) {  // always force frozen
                if (Get_Cell_Flag(v, CONST))
                    return FAIL(Error_Alias_Constrains_Raw());
                Freeze_Flex(s);
            }
        }
        return GENERIC_CFUNC(TO, Any_String)(LEVEL);  // not optimized yet
    }

    if (as == TYPE_BLANK) {
        Size size;
        Cell_Utf8_Size_At(&size, v);
        if (size == 0)
            return Init_Blank(OUT);
        return RAISE("Can only AS/TO convert empty series to BLANK!");
    }

    return UNHANDLED;
}


IMPLEMENT_GENERIC(PICK, Is_Issue)
{
    INCLUDE_PARAMS_OF_PICK;

    const Element* issue = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    if (not Is_Integer(picker))
        return FAIL(PARAM(PICKER));

    REBI64 n = VAL_INT64(picker);
    if (n <= 0)
        return RAISE(Error_Bad_Pick_Raw(picker));

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, issue);
    if (n > len)
        return nullptr;

    Codepoint c;
    cp = Utf8_Next(&c, cp);
    for (; n != 1; --n)
        cp = Utf8_Next(&c, cp);

    return Init_Char_Unchecked(OUT, c);
}


IMPLEMENT_GENERIC(REVERSE_OF, Any_Utf8)
{
    INCLUDE_PARAMS_OF_REVERSE_OF;

    Element* any_utf8 = Element_ARG(ELEMENT);
    Value* part = ARG(PART);

    Value* datatype = Copy_Cell(SPARE, Datatype_Of(any_utf8));

    return Delegate_Operation_With_Part(
        SYM_REVERSE, SYM_TEXT_X,
        Meta_Quotify(datatype), Quotify(any_utf8), Meta_Quotify(part)
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
    return NOTHING;
}


IMPLEMENT_GENERIC(RANDOM, Is_Issue)
{
    INCLUDE_PARAMS_OF_RANDOM;

    Element* issue = Element_ARG(MAX);

    if (not IS_CHAR(issue))
        return FAIL("RANDOM only for single-character ISSUE!");

    Codepoint c = Cell_Codepoint(issue);
    if (c == 0)
        return UNHANDLED;

    while (true) {
        Codepoint rand = cast(Codepoint, 1 + (Random_Int(REF(SECURE)) % c));

        Option(Error*) e = Trap_Init_Char(OUT, rand);
        if (not e)
            break;
        unnecessary(Free_Unmanaged_Flex(unwrap e));  // errors are prealloc'd
    }

    return OUT;
}


IMPLEMENT_GENERIC(SHUFFLE_OF, Any_Utf8)
{
    INCLUDE_PARAMS_OF_SHUFFLE_OF;

    Element* any_utf8 = Element_ARG(ELEMENT);
    Value* part = ARG(PART);

    if (REF(SECURE))
        return FAIL(Error_Bad_Refines_Raw());

    Value* datatype = Copy_Cell(SPARE, Datatype_Of(any_utf8));

    return Delegate_Operation_With_Part(
        SYM_SHUFFLE, SYM_TEXT_X,
        Meta_Quotify(datatype), Quotify(any_utf8), Meta_Quotify(part)
    );
}


//
//  /codepoint-of: native:generic [
//
//  "Get the singular codepoint that an ISSUE! or BINARY! correspond to"
//
//      return: [~null~ integer!]
//      element [<maybe> fundamental?]
//  ]
//
DECLARE_NATIVE(CODEPOINT_OF)
{
    INCLUDE_PARAMS_OF_CODEPOINT_OF;

    return Dispatch_Generic(CODEPOINT_OF, Element_ARG(ELEMENT), LEVEL);
}


IMPLEMENT_GENERIC(CODEPOINT_OF, Is_Issue)
{
    INCLUDE_PARAMS_OF_CODEPOINT_OF;

    Element* issue = Element_ARG(ELEMENT);
    assert(Is_Issue(issue));

    if (
        Stringlike_Has_Node(issue)
        or issue->extra.at_least_4[IDX_EXTRA_LEN] != 1
    ){
        return RAISE(Error_Not_One_Codepoint_Raw());
    }
    return Init_Integer(OUT, Cell_Codepoint(issue));
}


IMPLEMENT_GENERIC(LENGTH_OF, Any_Utf8)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* v = Element_ARG(ELEMENT);
    possibly(Any_Word(v));  // !!! should WORD! disallow LENGTH OF ?

    REBLEN len;
    Cell_Utf8_Len_Size_At(&len, nullptr, v);
    return Init_Integer(OUT, len);
}


IMPLEMENT_GENERIC(SIZE_OF, Any_Utf8)
{
    INCLUDE_PARAMS_OF_SIZE_OF;

    Element* v = Element_ARG(ELEMENT);
    possibly(Any_String(v));  // delegates here
    possibly(Any_Word(v));  // !!! should WORD! disable `size of`?

    Size size;
    Cell_Utf8_Size_At(&size, v);
    return Init_Integer(OUT, size);
}


//
//  /trailing-bytes-for-utf8: native [
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
        return FAIL(Error_Out_Of_Range(ARG(FIRST_BYTE)));

    uint_fast8_t trail = g_trailing_bytes_for_utf8[cast(Byte, byte)];
    if (trail > 3 and not REF(EXTENDED)) {
        assert(trail == 4 or trail == 5);
        return FAIL(
            "Use :EXTENDED with TRAILING-BYTES-FOR-UTF-8 for 4 or 5 bytes"
        );
    }

    return Init_Integer(OUT, trail);
}
