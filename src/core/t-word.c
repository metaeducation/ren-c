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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
REB_R MAKE_Word(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (ANY_WORD(arg)) {
        //
        // !!! This only reset the type, not header bits...as it used to be
        // that header bits related to the binding state.  That's no longer
        // true since EXTRA(Binding, ...) conveys the entire bind state.
        // Rethink what it means to preserve the bits vs. not.
        //
        Move_Value(out, arg);
        mutable_KIND_BYTE(out) = kind;
        return out;
    }

    if (ANY_STRING(arg)) {
        REBSIZ size;
        REBYTE *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_WORD);

        if (kind == REB_ISSUE) {
            if (NULL == Scan_Issue(out, bp, size))
                fail (Error_Bad_Char_Raw(arg));
        }
        else {
            if (NULL == Scan_Any_Word(out, kind, bp, size))
                fail (Error_Bad_Char_Raw(arg));
        }
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
    // !!! Compatibility hack for old TO WORD! of LIT-WORD!
    //
    if (IS_QUOTED(arg)) {
        DECLARE_LOCAL (dequoted);
        Dequotify(Move_Value(dequoted, arg));
        return MAKE_Word(out, kind, dequoted);
    }
    return MAKE_Word(out, kind, arg);
}


inline static void Mold_Word(REB_MOLD *mo, const REBCEL *v)
{
    REBSTR *spelling = VAL_WORD_SPELLING(v);
    const char *head = STR_HEAD(spelling); // UTF-8
    size_t size = STR_SIZE(spelling); // number of UTF-8 bytes
    Append_Utf8_Utf8(mo->series, head, size);
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
    Append_Utf8_Codepoint(mo->series, ':');
}


//
//  MF_Get_word: C
//
void MF_Get_word(REB_MOLD *mo, const REBCEL *v, bool form) {
    UNUSED(form);
    Append_Utf8_Codepoint(mo->series, ':');
    Mold_Word(mo, v);
}


//
//  MF_Lit_word: C
//
// !!! Note: will be deprecated by generic backslash literals.
//
void MF_Lit_word(REB_MOLD *mo, const REBCEL *v, bool form) {
    UNUSED(form);
    Append_Utf8_Codepoint(mo->series, '\'');
    Mold_Word(mo, v);
}


//
//  MF_Refinement: C
//
void MF_Refinement(REB_MOLD *mo, const REBCEL *v, bool form) {
    UNUSED(form);
    Append_Utf8_Codepoint(mo->series, '/');
    Mold_Word(mo, v);
}


//
//  MF_Issue: C
//
void MF_Issue(REB_MOLD *mo, const REBCEL *v, bool form) {
    UNUSED(form);
    Append_Utf8_Codepoint(mo->series, '#');
    Mold_Word(mo, v);
}


//
//  PD_Word: C
//
// !!! The eventual intention is that words will become ANY-STRING!s, and
// support the same operations.  As a small step in that direction, this
// adds support for picking characters out of the UTF-8 data of a word
// (eventually all strings will be "UTF-8 Everywhere")
//
REB_R PD_Word(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    REBSTR *str = VAL_WORD_SPELLING(pvs->out);

    if (not opt_setval) { // PICK-ing
        if (IS_INTEGER(picker)) {
            REBINT n = Int32(picker) - 1;
            if (n < 0)
                return nullptr;

            REBSIZ size = SER_LEN(str);
            const REBYTE *bp = cb_cast(STR_HEAD(str));
            REBUNI c;
            do {
                if (size == 0)
                    return nullptr; // character asked for is past end

                if (*bp < 0x80)
                    c = *bp;
                else
                    bp = Back_Scan_UTF8_Char(&c, bp, &size);
                --size;
                ++bp;
            } while (n-- != 0);

            Init_Char(pvs->out, c);
            return pvs->out;
        }

        fail ("ANY-WORD! picking only supports INTEGER!, currently");
    }

    fail ("Can't use ANY-WORD! with SET-PATH");
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
            const REBYTE *bp = cb_cast(STR_HEAD(spelling));
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

    fail (Error_Illegal_Action(VAL_TYPE(v), verb));
}
