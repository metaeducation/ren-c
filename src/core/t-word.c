//
//  File: %t-word.c
//  Summary: "word related datatypes"
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

#include "sys-core.h"


//
//  Compare_Spellings: C
//
// Used in CT_Word() and CT_Bad_Word()
//
REBINT Compare_Spellings(const Symbol* a, const Symbol* b, bool strict)
{
    if (strict) {
        if (a == b)
            return 0;

        // !!! "Strict" is interpreted as "case-sensitive comparison".  Using
        // strcmp() means the two pointers must be to '\0'-terminated byte
        // arrays, and they are checked byte-for-byte.  This does not account
        // for unicode normalization.  Review.
        //
        // https://en.wikipedia.org/wiki/Unicode_equivalence#Normalization
        //
        REBINT diff = strcmp(String_UTF8(a), String_UTF8(b));  // byte match check
        if (diff == 0)
            return 0;
        return diff > 0 ? 1 : -1;  // strcmp result not strictly in [-1 0 1]
    }
    else {
        // Different cases acceptable, only check for a canon match
        //
        if (Are_Synonyms(a, b))
            return 0;

        // !!! "They must differ by case...."  This needs to account for
        // unicode "case folding", as well as "normalization".
        //
        REBINT diff = Compare_UTF8(String_Head(a), String_Head(b), String_Size(b));
        if (diff >= 0) {
            assert(diff == 0 or diff == 1 or diff == 3);
            return 0;  // non-case match
        }
        assert(diff == -1 or diff == -3);  // no match
        return diff + 2;
    }
}


//
//  CT_Word: C
//
// Compare the names of two words and return the difference.
// Note that words are kept UTF8 encoded.
//
REBINT CT_Word(const Cell* a, const Cell* b, bool strict)
{
    return Compare_Spellings(
        Cell_Word_Symbol(a),
        Cell_Word_Symbol(b),
        strict
    );
}


//
//  MAKE_Word: C
//
Bounce MAKE_Word(
    Level* level_,
    Kind k,
    Option(const Value*) parent,
    const REBVAL *arg
){
    Heart heart = cast(Heart, k);

    if (parent)
        fail (Error_Bad_Make_Parent(heart, unwrap(parent)));

    if (Any_Word(arg)) {
        //
        // !!! This only reset the type, not header bits...as it used to be
        // that header bits related to the binding state.  That's no longer
        // true since EXTRA(Binding, ...) conveys the entire bind state.
        // Rethink what it means to preserve the bits vs. not.
        //
        Copy_Cell(OUT, arg);
        if (heart != REB_WORD) {
            const Symbol* sym = Cell_Word_Symbol(arg);
            if (Get_Subclass_Flag(SYMBOL, sym, ILLEGAL_WITH_SIGIL)) {
                HEART_BYTE(OUT) = REB_WORD;
                fail (Error_Illegal_Sigil_Word_Raw(OUT));
            }
        }
        HEART_BYTE(OUT) = heart;
        return OUT;
    }

    if (Any_String(arg)) {
        if (Is_Series_Frozen(Cell_String(arg)))
            goto as_word;  // just reuse AS mechanics on frozen strings

        // Otherwise, we'll have to copy the data for a TO conversion
        //
        // !!! Note this permits `TO WORD! "    spaced-out"` ... it's not
        // clear that it should do so.  Review `Analyze_String_For_Scan()`

        Size size;
        const Byte* bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_WORD);

        if (NULL == Scan_Any_Word(OUT, heart, bp, size))
            return RAISE(Error_Bad_Char_Raw(arg));

        return OUT;
    }
    else if (Is_Issue(arg)) {
        //
        // Run the same mechanics that AS WORD! would, since it's immutable.
        //
      as_word: {
        REBVAL *as = rebValue("as", Datatype_From_Kind(heart), arg);
        Copy_Cell(OUT, as);
        rebRelease(as);

        return OUT;
      }
    }
    else if (Is_Logic(arg)) {
        return Init_Any_Word(
            OUT,
            heart,
            Cell_Logic(arg) ? Canon(TRUE) : Canon(FALSE)
        );
    }

    return RAISE(Error_Unexpected_Type(REB_WORD, VAL_TYPE(arg)));
}


//
//  TO_Word: C
//
Bounce TO_Word(Level* level_, Kind k, const REBVAL *arg)
{
    Heart heart = cast(Heart, k);

    // This is here to convert `to word! /a` into `a`.  It also allows
    // `to word! ////a////` and variants, because it seems interesting to try
    // that vs. erroring for a bit, to see if it turns out to be useful.
    //
    // !!! This seems like something TO does more generally, e.g.
    // `to integer! /"10"` making 10.  We might call these "solo paths" as
    // a generalization of "refinement paths"
    //
    if (Is_Path(arg) or Is_Tuple(arg)) {
        FRESHEN(OUT);

        DECLARE_LOCAL (temp);

        REBLEN len = Cell_Sequence_Len(arg);
        REBLEN i;
        for (i = 0; i < len; ++i) {
            const Element* item = Copy_Sequence_At(temp, arg, i);
            if (Is_Blank(item))
                continue;
            if (not Is_Word(item))
                return RAISE(
                    "Can't make ANY-WORD? from path unless it's one WORD!"
                );
            if (not Is_Fresh(OUT))
                return RAISE(
                    "Can't make ANY-WORD? from path w/more than one WORD!"
                );
            Derelativize(OUT, item, Cell_Sequence_Specifier(arg));
        }

        if (Is_Fresh(OUT))
            return RAISE("Can't MAKE ANY-WORD? from PATH! that's all BLANK!s");

        HEART_BYTE(OUT) = heart;
        return OUT;
    }

    if (Any_Array(arg)) {
        if (Cell_Series_Len_At(arg) != 1)
            return RAISE("Can't TO ANY-WORD? on array with length > 1");
        const Element* item = Cell_Array_Len_At(nullptr, arg);
        if (not Is_Word(item))
            return RAISE("TO ANY-WORD? requires array with one word in it");
        Copy_Cell(OUT, item);
        HEART_BYTE(OUT) = heart;
        return OUT;
    }

    return MAKE_Word(level_, heart, nullptr, arg);
}


//
//  MF_Word: C
//
void MF_Word(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form);
    const Symbol* symbol = Cell_Word_Symbol(v);
    Append_Utf8(mo->series, String_UTF8(symbol), String_Size(symbol));
}


inline static void Mold_Decorable_Word(REB_MOLD *mo, const Cell* v)
{
    const Symbol* symbol = Cell_Word_Symbol(v);
    assert(Not_Subclass_Flag(SYMBOL, symbol, ILLEGAL_WITH_SIGIL));
    Append_Utf8(mo->series, String_UTF8(symbol), String_Size(symbol));
}


//
//  MF_Set_Word: C
//
void MF_Set_Word(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form);
    Mold_Decorable_Word(mo, v);
    Append_Codepoint(mo->series, ':');
}


//
//  MF_Get_Word: C
//
void MF_Get_Word(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form);
    Append_Codepoint(mo->series, ':');
    Mold_Decorable_Word(mo, v);
}


//
//  MF_Meta_Word: C
//
void MF_Meta_Word(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form);
    Append_Codepoint(mo->series, '^');
    Mold_Decorable_Word(mo, v);
}


//
//  MF_The_Word: C
//
void MF_The_Word(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form);
    Append_Codepoint(mo->series, '@');
    Mold_Decorable_Word(mo, v);
}


//
//  MF_Var_Word: C
//
void MF_Var_Word(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form);
    Append_Codepoint(mo->series, '$');
    Mold_Decorable_Word(mo, v);
}

//
//  MF_Type_Word: C
//
void MF_Type_Word(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form);
    Append_Codepoint(mo->series, '&');
    Mold_Decorable_Word(mo, v);
}


//
//  REBTYPE: C
//
// The future plan for WORD! types is that they will be unified somewhat with
// strings...but that bound words will have read-only data.  Under such a
// plan, string-converting words would not be necessary for basic textual
// operations.
//
REBTYPE(Word)
{
    REBVAL *v = D_ARG(1);
    assert(Any_Word(v));

    switch (Symbol_Id(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        Option(SymId) property = Cell_Word_Id(ARG(property));

        switch (property) {
          case SYM_LENGTH: {  // byte size stored, but not # of codepoints
            const String* spelling = Cell_Word_Symbol(v);
            Utf8(const*) cp = String_Head(spelling);
            Size size = String_Size(spelling);
            Length len = 0;
            for (; size > 0; cp = Skip_Codepoint(cp)) {  // manually walk codepoints
                size = size - 1;
                len = len + 1;
            }
            return Init_Integer(OUT, len); }

          case SYM_BINDING: {
            if (not Did_Get_Binding_Of(OUT, v))
                return nullptr;

            return OUT; }

          default:
            break;
        }
        break; }

      case SYM_COPY:
        return COPY(v);

      default:
        break;
    }

    fail (UNHANDLED);
}
