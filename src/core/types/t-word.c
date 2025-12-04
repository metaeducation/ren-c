//
//  file: %t-word.c
//  summary: "word related datatypes"
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
        REBINT diff = strcmp(Strand_Utf8(a), Strand_Utf8(b));  // byte match check
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
        REBINT diff = Compare_UTF8(Strand_Head(a), Strand_Head(b), Strand_Size(b));
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
REBINT CT_Word(const Element* a, const Element* b, bool strict)
{
    return Compare_Spellings(Word_Symbol(a), Word_Symbol(b), strict);
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

    Heart heart = Datatype_Builtin_Heart(ARG(TYPE));
    assert(heart == TYPE_WORD);

    Element* arg = Element_ARG(DEF);

    if (not Any_Sequence(arg))
        return fail (Error_Bad_Make(heart, arg));

  make_word_from_sequence: {

    // (make word! '/a) or (make word! 'a:) etc.

    attempt {
        Unsingleheart_Sequence(arg) except (Error* e) {
            UNUSED(e);
            break;
        }

        if (Any_Sequence(arg))
            again;

        if (not Any_Word(arg))
            break;
    }
    then {
        KIND_BYTE(arg) = heart;
        return COPY(arg);
    }

    return fail (
        "Can't MAKE ANY-WORD? from sequence unless it wraps one WORD!"
    );
}}


IMPLEMENT_GENERIC(MOLDIFY, Is_Word)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);

    Append_Spelling(mo->strand, Word_Symbol(v));

    return TRASH;
}


// WORD!s as a subset of string don't have any particular separate rules
// for TO conversions that immutable strings don't have (and strings may
// be aliases of words, so TO conversions of strings to word may be able
// to reuse the symbol underlying the string).
//
IMPLEMENT_GENERIC(TO, Is_Word)
{
    INCLUDE_PARAMS_OF_TO;

    USED(ARG(VALUE));  // deferred to other generic implementations

    Heart to = Datatype_Builtin_Heart(ARG(TYPE));

    if (to == TYPE_WORD)
        return GENERIC_CFUNC(AS, Is_Word)(LEVEL);  // immutable alias

    if (Any_String_Type(to))  // need mutable copy
        return GENERIC_CFUNC(TO, Any_Utf8)(LEVEL);

    if (Any_Utf8_Type(to))
        return GENERIC_CFUNC(AS, Is_Word)(LEVEL);  // non-string, immutable

    return GENERIC_CFUNC(TO, Any_Utf8)(LEVEL);  // TO INTEGER!, etc.
}

//
//  Alias_Any_Word_As: C
//
Result(Element*) Alias_Any_Word_As(
    Sink(Element) out,
    const Element* word,
    Heart as
){
    if (as == TYPE_WORD) {
        Copy_Cell(out, word);
        Plainify(out);
        return out;
    }

    if (Any_String_Type(as))  // will be an immutable string
        return Init_Any_String(out, as, Word_Symbol(word));

    if (as == TYPE_RUNE) {  // immutable (note no EMAIL! or URL! possible)
        const Symbol* s = Word_Symbol(word);
        if (Try_Init_Small_Utf8(  // invariant: fit in cell if it can
            out,
            as,
            Strand_Head(s),
            Strand_Len(s),
            Strand_Size(s)
        )){
            return out;
        }
        return Init_Any_String(out, as, s);
    }

    if (as == TYPE_BLOB)  // will be an immutable blob
        return Init_Blob(out, Word_Symbol(word));

    return fail (Error_Invalid_Type(as));
}


IMPLEMENT_GENERIC(AS, Is_Word)
{
    INCLUDE_PARAMS_OF_AS;

    require (
      Alias_Any_Word_As(
        OUT,
        Element_ARG(VALUE),
        Datatype_Builtin_Heart(ARG(TYPE))
    ));

    return OUT;
}


IMPLEMENT_GENERIC(BINDING_OF, Is_Word)
{
    INCLUDE_PARAMS_OF_BINDING_OF;

    Element* any_word = Element_ARG(VALUE);

    if (not Try_Get_Binding_Of(OUT, any_word))
        return NULLED;

    return OUT;
}
