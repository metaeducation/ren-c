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

    Heart heart = Cell_Datatype_Builtin_Heart(ARG(TYPE));
    assert(Any_Word_Type(heart));

    Element* arg = Element_ARG(DEF);

    if (Any_Sequence(arg)) {  // (make word! '/a) or (make word! 'a:) etc.
        do {
            Option(Error*) error = Trap_Unsingleheart(arg);
            if (error)
                goto sequence_didnt_devolve_to_word;
        } while (Any_Sequence(arg));

        if (Any_Word(arg)) {
            HEART_BYTE(arg) = heart;
            return COPY(arg);
        }

      sequence_didnt_devolve_to_word:
        return RAISE(
            "Can't MAKE ANY-WORD? from sequence unless it wraps one WORD!"
        );
    }

    return RAISE(Error_Bad_Make(heart, arg));
}


IMPLEMENT_GENERIC(MOLDIFY, Any_Word)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);

    Option(Sigil) sigil = Sigil_For_Heart(Heart_Of(v));
    if (sigil)
        Append_Codepoint(mo->string, Char_For_Sigil(unwrap sigil));

    Append_Spelling(mo->string, Cell_Word_Symbol(v));

    return TRASH;
}


// WORD!s as a subset of string don't have any particular separate rules
// for TO conversions that immutable strings don't have (and strings may
// be aliases of words, so TO conversions of strings to word may be able
// to reuse the symbol underlying the string).
//
IMPLEMENT_GENERIC(TO, Any_Word)
{
    INCLUDE_PARAMS_OF_TO;

    USED(ARG(ELEMENT));  // deferred to other generic implementations

    Heart to = Cell_Datatype_Builtin_Heart(ARG(TYPE));

    if (Any_Word_Type(to))
        return GENERIC_CFUNC(AS, Any_Word)(LEVEL);  // immutable alias

    if (Any_String_Type(to))  // need mutable copy
        return GENERIC_CFUNC(TO, Any_Utf8)(LEVEL);

    if (Any_Utf8_Type(to))
        return GENERIC_CFUNC(AS, Any_Word)(LEVEL);  // non-string, immutable

    return GENERIC_CFUNC(TO, Any_Utf8)(LEVEL);  // TO INTEGER!, etc.
}

//
//  Trap_Alias_Any_Word_As: C
//
Option(Error*) Trap_Alias_Any_Word_As(
    Sink(Element) out,
    const Element* word,
    Heart as
){
    if (Any_Word_Type(as)) {
        Copy_Cell(out, word);
        HEART_BYTE(out) = as;
        return SUCCESS;
    }

    if (Any_String_Type(as)) {  // will be an immutable string
        Init_Any_String(out, as, Cell_Word_Symbol(word));
        return SUCCESS;
    }

    if (as == TYPE_ISSUE) {  // immutable (note no EMAIL! or URL! possible)
        const Symbol* s = Cell_Word_Symbol(word);
        if (Try_Init_Small_Utf8(  // invariant: fit in cell if it can
            out,
            as,
            String_Head(s),
            String_Len(s),
            String_Size(s)
        )){
            return SUCCESS;
        }
        Init_Any_String(out, as, s);
        return SUCCESS;
    }

    if (as == TYPE_BLOB) {  // will be an immutable blob
        Init_Blob(out, Cell_Word_Symbol(word));
        return SUCCESS;
    }

    return Error_Invalid_Type(as);
}


IMPLEMENT_GENERIC(AS, Any_Word)
{
    INCLUDE_PARAMS_OF_AS;

    Option(Error*) e = Trap_Alias_Any_Word_As(
        OUT,
        Element_ARG(ELEMENT),
        Cell_Datatype_Builtin_Heart(ARG(TYPE))
    );
    if (e)
        return FAIL(unwrap e);

    return OUT;
}


IMPLEMENT_GENERIC(BINDING_OF, Any_Word)
{
    INCLUDE_PARAMS_OF_BINDING_OF;

    Element* any_word = Element_ARG(ELEMENT);

    if (not Try_Get_Binding_Of(OUT, any_word))
        return nullptr;

    return OUT;
}
