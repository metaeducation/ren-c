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
//  CT_Word: C
//
// Compare the names of two words and return the difference.
// Note that words are kept UTF8 encoded.
//
REBINT CT_Word(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    const REBYTE *sp = STR_HEAD(VAL_WORD_SPELLING(a));
    const REBYTE *tp = STR_HEAD(VAL_WORD_SPELLING(b));

    if (strict) {
        if (VAL_WORD_SPELLING(a) == VAL_WORD_SPELLING(b))
            return 0;

        // !!! "Strict" is interpreted as "case-sensitive comparison".  Using
        // strcmp() means the two pointers must be to '\0'-terminated byte
        // arrays, and they are checked byte-for-byte.  This does not account
        // for unicode normalization.  Review.
        //
        // https://en.wikipedia.org/wiki/Unicode_equivalence#Normalization
        //
        REBINT diff = strcmp(cs_cast(sp), cs_cast(tp));  // byte match check
        if (diff == 0)
            return 0;
        return diff > 0 ? 1 : -1;  // not strictly in [-1 0 1]
    }
    else {
        // Different cases acceptable, only check for a canon match
        //
        if (VAL_WORD_CANON(a) == VAL_WORD_CANON(b))
            return 0;

        // !!! "They must differ by case...."  This needs to account for
        // unicode "case folding", as well as "normalization".
        //
        REBINT diff = Compare_UTF8(sp, tp, strsize(tp)) + 2;
        assert(diff == 0 or diff == -1 or diff == 1);
        return diff;
    }
}


//
//  MAKE_Word: C
//
REB_R MAKE_Word(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (ANY_WORD(arg)) {
        //
        // !!! This only reset the type, not header bits...as it used to be
        // that header bits related to the binding state.  That's no longer
        // true since EXTRA(Binding, ...) conveys the entire bind state.
        // Rethink what it means to preserve the bits vs. not.
        //
        Move_Value(out, arg);
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = kind;
        return out;
    }

    if (ANY_STRING(arg)) {
        REBSIZ size;
        const REBYTE *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_WORD);

        if (NULL == Scan_Any_Word(out, kind, bp, size))
            fail (Error_Bad_Char_Raw(arg));

        return out;
    }
    else if (IS_CHAR(arg)) {  // CHAR! caches the UTF-8 encoding in the cell
        const REBYTE *encoded = VAL_CHAR_ENCODED(arg);
        REBSIZ encoded_size = VAL_CHAR_ENCODED_SIZE(arg);

        if (nullptr == Scan_Any_Word(out, kind, encoded, encoded_size))
            fail (Error_Bad_Char_Raw(arg));
        return out;
    }
    else if (IS_DATATYPE(arg)) {
        return Init_Any_Word(out, kind, Canon(VAL_TYPE_SYM(arg)));
    }
    else if (IS_LOGIC(arg)) {
        return Init_Any_Word(
            out,
            kind,
            VAL_LOGIC(arg) ? Canon(SYM_TRUE) : Canon(SYM_FALSE)
        );
    }

    fail (Error_Unexpected_Type(REB_WORD, VAL_TYPE(arg)));
}


//
//  TO_Word: C
//
REB_R TO_Word(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    // This is here to convert `to word! /a` into `a`.  It also allows
    // `to word! ////a////` and variants, because it seems interesting to try
    // that vs. erroring for a bit, to see if it turns out to be useful.
    //
    // !!! This seems like something TO does more generally, e.g.
    // `to integer! /"10"` making 10.  We might call these "solo paths" as
    // a generalization of "refinement paths"
    //
    if (IS_PATH(arg)) {
        SET_END(out);

        DECLARE_LOCAL (temp);

        REBLEN len = VAL_SEQUENCE_LEN(arg);
        REBLEN i;
        for (i = 0; i < len; ++i) {
            const RELVAL *item = VAL_SEQUENCE_AT(temp, arg, i);
            if (IS_BLANK(item))
                continue;
            if (not IS_WORD(item))
                fail ("Can't make ANY-WORD! from path unless it's one WORD!");
            if (not IS_END(out))
                fail ("Can't make ANY-WORD! from path w/more than one WORD!");
            Derelativize(out, item, VAL_SEQUENCE_SPECIFIER(arg));
        }

        if (IS_END(out))
            fail ("Can't MAKE ANY-WORD! from PATH! that's all BLANK!s");

        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = kind;
        return out;
    }

    return MAKE_Word(out, kind, nullptr, arg);
}


inline static void Mold_Word(REB_MOLD *mo, REBCEL(const*) v)
{
    REBSTR *spelling = VAL_WORD_SPELLING(v);
    Append_Utf8(mo->series, STR_UTF8(spelling), STR_SIZE(spelling));
}


//
//  MF_Word: C
//
void MF_Word(REB_MOLD *mo, REBCEL(const*) v, bool form) {
    UNUSED(form);
    Mold_Word(mo, v);
}


//
//  MF_Set_word: C
//
void MF_Set_word(REB_MOLD *mo, REBCEL(const*) v, bool form) {
    UNUSED(form);
    Mold_Word(mo, v);
    Append_Codepoint(mo->series, ':');
}


//
//  MF_Get_word: C
//
void MF_Get_word(REB_MOLD *mo, REBCEL(const*) v, bool form) {
    UNUSED(form);
    Append_Codepoint(mo->series, ':');
    Mold_Word(mo, v);
}


//
//  MF_Sym_word: C
//
void MF_Sym_word(REB_MOLD *mo, REBCEL(const*) v, bool form) {
    UNUSED(form);
    Append_Codepoint(mo->series, '@');
    Mold_Word(mo, v);
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
    assert(ANY_WORD(v));

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH: {
            REBSTR *spelling = VAL_WORD_SPELLING(v);
            const REBYTE *bp = STR_HEAD(spelling);
            REBSIZ size = STR_SIZE(spelling);
            REBLEN len = 0;
            for (; size > 0; ++bp, --size) {
                if (*bp < 0x80)
                    ++len;
                else {
                    REBUNI uni;
                    if ((bp = Back_Scan_UTF8_Char(&uni, bp, &size)) == NULL)
                        fail (Error_Bad_Utf8_Raw());
                    ++len;
               }
            }
            return Init_Integer(D_OUT, len); }

          case SYM_BINDING: {
            if (Did_Get_Binding_Of(D_OUT, v))
                return D_OUT;
            return nullptr; }

          default:
            break;
        }
        break; }

      case SYM_COPY:
        RETURN (v);

      default:
        break;
    }

    return R_UNHANDLED;
}
