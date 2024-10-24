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
    const Value* arg
){
    Heart heart = cast(Heart, k);

    if (parent)
        return FAIL(Error_Bad_Make_Parent(heart, unwrap parent));

    if (Any_Word(arg)) {
        Copy_Cell(OUT, arg);
        HEART_BYTE(OUT) = heart;
        return OUT;
    }

    if (Any_String(arg)) {
        if (Is_Flex_Frozen(Cell_String(arg)))
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
        Value* as = rebValue("as", Datatype_From_Kind(heart), arg);
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
Bounce TO_Word(Level* level_, Kind k, const Value* arg)
{
    Heart heart = cast(Heart, k);

    if (Any_Sequence(arg)) {  // (to word! '/a) or (to word! 'a:) etc.
        Copy_Cell(OUT, arg);
        do {
            Option(Error*) error = Trap_Unsingleheart(cast(Element*, OUT));
            if (error)
                goto sequence_didnt_decay_to_word;
        } while (Any_Sequence(OUT));

        if (Any_Word(OUT))
            return OUT;

      sequence_didnt_decay_to_word:

        return RAISE(
            "Can't make ANY-WORD? from sequence unless it's one WORD!"
        );
    }

    if (Any_List(arg)) {
        if (Cell_Series_Len_At(arg) != 1)
            return RAISE("Can't TO ANY-WORD? on list with length > 1");
        const Element* item = Cell_List_Len_At(nullptr, arg);
        if (not Is_Word(item))
            return RAISE("TO ANY-WORD? requires list with one word in it");
        Copy_Cell(OUT, item);
        HEART_BYTE(OUT) = heart;
        return OUT;
    }

    return MAKE_Word(level_, heart, nullptr, arg);
}


//
//  MF_Word: C
//
void MF_Word(Molder* mo, const Cell* v, bool form) {
    UNUSED(form);

    Option(Sigil) sigil = Sigil_Of_Kind(Cell_Heart(v));
    if (sigil)
        Append_Codepoint(mo->string, Symbol_For_Sigil(unwrap sigil));

    const Symbol* symbol = Cell_Word_Symbol(v);
    Append_Utf8(mo->string, String_UTF8(symbol), String_Size(symbol));
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
    Value* v = D_ARG(1);
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
            if (not Try_Get_Binding_Of(OUT, v))
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

    return UNHANDLED;
}
