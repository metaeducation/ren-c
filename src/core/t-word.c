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
IMPLEMENT_GENERIC(MAKE, Is_Word)
{
    INCLUDE_PARAMS_OF_MAKE;

    Heart heart = VAL_TYPE_HEART(ARG(type));
    assert(Any_Word_Kind(heart));

    Element* arg = Element_ARG(def);

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


IMPLEMENT_GENERIC(MOLDIFY, Any_Word)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(element);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(molder));
    bool form = REF(form);

    UNUSED(form);

    Option(Sigil) sigil = Sigil_Of_Kind(Cell_Heart(v));
    if (sigil)
        Append_Codepoint(mo->string, Symbol_For_Sigil(unwrap sigil));

    Append_Spelling(mo->string, Cell_Word_Symbol(v));

    return NOTHING;
}


IMPLEMENT_GENERIC(OLDGENERIC, Any_Word)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* word = cast(Element*, ARG_N(1));
    assert(Any_Word(word));

    switch (id) {

      case SYM_COPY:
        return COPY(word);

      default:
        break;
    }

    return UNHANDLED;
}


// WORD!s as a subset of string don't have any particular separate rules
// for TO conversions that immutable strings don't have (and strings may
// be aliases of words, so TO conversions of strings to word may be able
// to reuse the symbol underlying the string).
//
IMPLEMENT_GENERIC(TO, Any_Word)
{
    INCLUDE_PARAMS_OF_TO;

    USED(ARG(element));  // deferred to other generic implementations

    Heart to = VAL_TYPE_HEART(ARG(type));

    if (Any_Word_Kind(to))
        return GENERIC_CFUNC(AS, Any_Word)(LEVEL);  // immutable alias

    if (Any_String_Kind(to))  // need mutable copy
        return GENERIC_CFUNC(TO, Any_Utf8)(LEVEL);

    if (Any_Utf8_Kind(to))
        return GENERIC_CFUNC(AS, Any_Word)(LEVEL);  // non-string, immutable

    return GENERIC_CFUNC(TO, Any_Utf8)(LEVEL);  // TO INTEGER!, etc.
}


IMPLEMENT_GENERIC(AS, Any_Word)
{
    INCLUDE_PARAMS_OF_AS;

    Element* word = Element_ARG(element);
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

    return UNHANDLED;
}


IMPLEMENT_GENERIC(BINDING_OF, Any_Word)
{
    INCLUDE_PARAMS_OF_BINDING_OF;

    Element* any_word = Element_ARG(element);

    if (not Try_Get_Binding_Of(OUT, any_word))
        return nullptr;

    return OUT;
}
