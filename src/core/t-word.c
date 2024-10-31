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
// But MAKE has the ability to heed bindings and do evaluations.  So it seems
// that this shorthand is useful:
//
//     as word! unspaced [...]
//     ->
//     make word! [...]  ; saves 8 characters
//
// It doesn't seem to do a lot of good to have (make word! "some-string") as
// an alternative to (to word! "some-string") or (as word! "some-string").
// Those two choices have nuance in them, e.g. freezing and reusing the
// string vs. copying it, and adding make into the mix doesn't really help.
//
// There might be applications of things like (make word! 241) being a way
// of creating a word based on its symbol ID.  But generally speaking, it's
// hard to think of anything besides [...] and @[...] being useful.
//
Bounce Makehook_Word(Level* level_, Kind k, Element* arg) {
    assert(Any_Word_Kind(k));

    if (Is_Block(arg) or Is_The_Block(arg))
        return rebValue(Canon(AS), Datatype_From_Kind(k), "unspaced", rebQ(arg));

    if (Any_Sequence(arg)) {  // (make word! '/a) or (make word! 'a:) etc.
        do {
            Option(Error*) error = Trap_Unsingleheart(arg);
            if (error)
                goto sequence_didnt_decay_to_word;
        } while (Any_Sequence(arg));

        if (Any_Word(arg)) {
            HEART_BYTE(arg) = k;
            return COPY(arg);
        }

      sequence_didnt_decay_to_word:
        return RAISE(
            "Can't MAKE ANY-WORD? from sequence unless it wraps one WORD!"
        );
    }

    return RAISE(Error_Bad_Make(k, arg));
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
//  REBTYPE: C
//
// The future plan for WORD! types is that they will be unified somewhat with
// strings...but that bound words will have read-only data.  Under such a
// plan, string-converting words would not be necessary for basic textual
// operations.
//
REBTYPE(Word)
{
    Value* word = D_ARG(1);
    assert(Any_Word(word));

    switch (Symbol_Id(verb)) {
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
            assert(*cp == '\0');
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
    // to reuse the symbol underlying the string).  Delegate to common code.

      case SYM_TO_P:
        return T_String(level_, verb);

      default:
        break;
    }

    return UNHANDLED;
}
