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
//  Makehook_Word: C
//
// Historically, WORD! creation was done with AS and TO.
//
// (make word! [...]) was considered to mean (as word! unspaced [...]), but
// this responsibility was moved to (join word! [...])
//
// It doesn't seem to do a lot of good to have (make word! "some-string") as
// an alternative to (to word! "some-string") or (as word! "some-string").
// Those two choices have nuance in them, e.g. freezing and reusing the
// string vs. copying it, and adding make into the mix doesn't really help.
//
// There might be applications of things like (make word! 241) being a way
// of creating a word based on its symbol ID.
//
Bounce Makehook_Word(Level* level_, Heart heart, Element* arg) {
    assert(Any_Word_Kind(heart));

    if (Any_Sequence(arg)) {  // (make word! '/a) or (make word! 'a:) etc.
        do {
            Option(Error*) error = Trap_Unsingleheart(arg);
            if (error)
                goto sequence_didnt_decay_to_word;
        } while (Any_Sequence(arg));

        if (Any_Word(arg)) {
            HEART_BYTE(arg) = heart;
            return COPY(arg);
        }

      sequence_didnt_decay_to_word:
        return RAISE(
            "Can't MAKE ANY-WORD? from sequence unless it wraps one WORD!"
        );
    }

    return RAISE(Error_Bad_Make(heart, arg));
}


//
//  MF_Word: C
//
void MF_Word(Molder* mo, const Cell* v, bool form) {
    UNUSED(form);

    Option(Sigil) sigil = Sigil_Of_Kind(Cell_Heart(v));
    if (sigil)
        Append_Codepoint(mo->string, Symbol_For_Sigil(unwrap sigil));

    Append_Spelling(mo->string, Cell_Word_Symbol(v));
}


//
//  DECLARE_GENERICS: C
//
// The future plan for WORD! types is that they will be unified somewhat with
// strings...but that bound words will have read-only data.  Under such a
// plan, string-converting words would not be necessary for basic textual
// operations.
//
DECLARE_GENERICS(Word)
{
    Option(SymId) id = Symbol_Id(verb);

    Element* word = cast(Element*,
        (id == SYM_TO or id == SYM_AS) ? ARG_N(2) : ARG_N(1)
    );
    assert(Any_Word(word));

    switch (id) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        Option(SymId) property = Cell_Word_Id(ARG(property));

        switch (property) {
          case SYM_LENGTH: {  // byte size stored, but not # of codepoints
            const String* spelling = Cell_Word_Symbol(word);
            Utf8(const*) cp = String_Head(spelling);
            Utf8(const*) tail = String_Tail(spelling);
            Length len = 0;
            for (; cp != tail; cp = Skip_Codepoint(cp))  // manually walk
                len = len + 1;
            assert(Codepoint_At(cp) == '\0');
            return Init_Integer(OUT, len); }

          case SYM_BINDING: {
            if (not Try_Get_Binding_Of(OUT, word))
                return nullptr;

            return OUT; }

          default:
            break;
        }
        break; }

      case SYM_COPY:
        return COPY(word);

    //=//// TO CONVERSIONS ////////////////////////////////////////////////=//

    // WORD!s as a subset of string don't have any particular separate rules
    // for TO conversions that immutable strings don't have (and strings may
    // be aliases of words, so TO conversions of strings to word may be able
    // to reuse the symbol underlying the string).

      case SYM_TO: {
        INCLUDE_PARAMS_OF_TO;
        UNUSED(ARG(element));  // word
        Heart to = VAL_TYPE_HEART(ARG(type));

        if (Any_Word_Kind(to))
            goto handle_as_conversion;

        if (Any_String_Kind(to))  // need mutable copy
            return T_Utf8(level_, verb);

        if (Any_Utf8_Kind(to))
            goto handle_as_conversion;

        return T_Utf8(level_, verb); }  // TO INTEGER!, etc.

    //=//// AS CONVERSIONS ////////////////////////////////////////////////=//

      handle_as_conversion:
      case SYM_AS: {
        INCLUDE_PARAMS_OF_AS;
        UNUSED(ARG(element));  // word
        Heart as = VAL_TYPE_HEART(ARG(type));

        if (Any_Word_Kind(as)) {
            Copy_Cell(OUT, word);
            HEART_BYTE(OUT) = as;
            return OUT;
        }

        if (Any_String_Kind(as))  // will be an immutable string
            return Init_Any_String(OUT, as, Cell_Word_Symbol(word));

        if (as == REB_ISSUE) {  // immutable (note no EMAIL! or URL! possible)
            const Symbol* s = Cell_Word_Symbol(word);
            if (Try_Init_Small_Utf8(  // invariant: fit in cell if it can
                OUT,
                as,
                String_Head(s),
                String_Len(s),
                String_Size(s)
            )){
                return OUT;
            }
            return Init_Any_String(OUT, as, s);
        }

        if (as == REB_BLOB)  // will be an immutable blob
            return Init_Blob(OUT, Cell_Word_Symbol(word));

        return UNHANDLED; }

      default:
        break;
    }

    return UNHANDLED;
}
