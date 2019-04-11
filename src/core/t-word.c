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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  CT_Word: C
//
// !!! The R3-Alpha code did a non-ordering comparison; it only tells whether
// the words are equal or not (1 or 0).  This creates bad invariants for
// sorting etc.  Review.
//
REBINT CT_Word(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    REBINT e;
    REBINT diff;
    if (mode >= 0) {
        if (mode == 1) {
            //
            // Symbols must be exact match, case-sensitively
            //
            if (VAL_WORD_SPELLING(a) != VAL_WORD_SPELLING(b))
                return 0;
        }
        else {
            // Different cases acceptable, only check for a canon match
            //
            if (VAL_WORD_CANON(a) != VAL_WORD_CANON(b))
                return 0;
        }

        return 1;
    }
    else {
        diff = Compare_Word(a, b, false);
        if (mode == -1) e = diff >= 0;
        else e = diff > 0;
    }
    return e;
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
    else if (IS_CHAR(arg)) {
        REBYTE buf[8];
        REBCNT len = Encode_UTF8_Char(&buf[0], VAL_CHAR(arg));
        if (NULL == Scan_Any_Word(out, kind, &buf[0], len))
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
        REBARR *a = VAL_ARRAY(arg);
        REBCNT index = 0;
        while (KIND_BYTE(ARR_AT(a, index)) == REB_BLANK)
            ++index;
        if (IS_END(ARR_AT(a, index)))
            fail ("Can't MAKE ANY-WORD! from PATH! that's all BLANK!s");

        RELVAL *non_blank = ARR_AT(a, index);
        ++index;
        while (KIND_BYTE(ARR_AT(a, index)) == REB_BLANK)
            ++index;

        if (NOT_END(ARR_AT(a, index)))
            fail ("Can't MAKE ANY-WORD! from PATH! with > 1 non-BLANK! item");

        DECLARE_LOCAL (solo);
        Derelativize(solo, non_blank, VAL_SPECIFIER(arg));
        return MAKE_Word(out, kind, nullptr, solo);
    }

    return MAKE_Word(out, kind, nullptr, arg);
}


inline static void Mold_Word(REB_MOLD *mo, const REBCEL *v)
{
    REBSTR *spelling = VAL_WORD_SPELLING(v);
    Append_Utf8(mo->series, STR_UTF8(spelling), STR_SIZE(spelling));
}


//
//  MF_Word: C
//
void MF_Word(REB_MOLD *mo, const REBCEL *v, bool form) {
    UNUSED(form);
    Mold_Word(mo, v);
}


//
//  MF_Set_word: C
//
void MF_Set_word(REB_MOLD *mo, const REBCEL *v, bool form) {
    UNUSED(form);
    Mold_Word(mo, v);
    Append_Codepoint(mo->series, ':');
}


//
//  MF_Get_word: C
//
void MF_Get_word(REB_MOLD *mo, const REBCEL *v, bool form) {
    UNUSED(form);
    Append_Codepoint(mo->series, ':');
    Mold_Word(mo, v);
}


//
//  MF_Sym_word: C
//
void MF_Sym_word(REB_MOLD *mo, const REBCEL *v, bool form) {
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
            REBCNT len = 0;
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
