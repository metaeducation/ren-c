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
REBINT CT_Word(const RELVAL *a, const RELVAL *b, REBINT mode)
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
        diff = Compare_Word(a, b, FALSE);
        if (mode == -1) e = diff >= 0;
        else e = diff > 0;
    }
    return e;
}


//
//  MAKE_Word: C
//
void MAKE_Word(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (ANY_WORD(arg)) {
        //
        // Only reset the type, not all the header bits (the bits must
        // stay in sync with the binding state)
        //
        Move_Value(out, arg);
        VAL_SET_TYPE_BITS(out, kind);
        return;
    }

    if (IS_STRING(arg)) {
        REBCNT len;
        const REBOOL allow_utf8 = TRUE;

        // Set name. Rest is set below.  If characters in the source
        // string are > 0x80 they will be encoded to UTF8 to be stored
        // in the symbol.
        //
        REBYTE *bp = Temp_Byte_Chars_May_Fail(
            arg, MAX_SCAN_WORD, &len, allow_utf8
        );

        if (kind == REB_ISSUE) {
            if (NULL == Scan_Issue(out, bp, len))
                fail (Error_Bad_Char_Raw(arg));
        }
        else {
            if (NULL == Scan_Any_Word(out, kind, bp, len))
                fail (Error_Bad_Char_Raw(arg));
            }
    }
    else if (IS_CHAR(arg)) {
        REBYTE buf[8];
        REBCNT len = Encode_UTF8_Char(&buf[0], VAL_CHAR(arg));
        if (NULL == Scan_Any_Word(out, kind, &buf[0], len))
            fail (Error_Bad_Char_Raw(arg));
    }
    else if (IS_DATATYPE(arg)) {
        Init_Any_Word(out, kind, Canon(VAL_TYPE_SYM(arg)));
    }
    else if (IS_LOGIC(arg)) {
        Init_Any_Word(
            out,
            kind,
            VAL_LOGIC(arg) ? Canon(SYM_TRUE) : Canon(SYM_FALSE)
        );
    }
    else
        fail (Error_Unexpected_Type(REB_WORD, VAL_TYPE(arg)));
}


//
//  TO_Word: C
//
void TO_Word(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Word(out, kind, arg);
}


//
//  MF_Word: C
//
void MF_Word(REB_MOLD *mo, const RELVAL *v, REBOOL form) {
    UNUSED(form); // no difference between MOLD and FORM at this time

    const REBYTE *spelling = STR_HEAD(VAL_WORD_SPELLING(v));
    REBCNT spelling_bytes = STR_NUM_BYTES(VAL_WORD_SPELLING(v));

    REBSER *s = mo->series;

    switch (VAL_TYPE(v)) {
    case REB_WORD: {
        Append_Utf8_Utf8(s, spelling, spelling_bytes);
        break; }

    case REB_SET_WORD:
        Append_Utf8_Utf8(s, spelling, spelling_bytes);
        Append_Utf8_Codepoint(s, ':');
        break;

    case REB_GET_WORD:
        Append_Utf8_Codepoint(s, ':');
        Append_Utf8_Utf8(s, spelling, spelling_bytes);
        break;

    case REB_LIT_WORD:
        Append_Utf8_Codepoint(s, '\'');
        Append_Utf8_Utf8(s, spelling, spelling_bytes);
        break;

    case REB_REFINEMENT:
        Append_Utf8_Codepoint(s, '/');
        Append_Utf8_Utf8(s, spelling, spelling_bytes);
        break;

    case REB_ISSUE:
        Append_Utf8_Codepoint(s, '#');
        Append_Utf8_Utf8(s, spelling, spelling_bytes);
        break;

    default:
        panic (v);
    }
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
    REBVAL *val = D_ARG(1);
    assert(ANY_WORD(val));

    switch (action) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH: {
            const REBYTE *bp = STR_HEAD(VAL_WORD_SPELLING(val));
            REBCNT len = 0;
            while (TRUE) {
                REBUNI ch;
                if ((bp = Back_Scan_UTF8_Char(&ch, bp, &len)) == NULL)
                    fail (Error_Bad_Utf8_Raw());
                if (ch == 0)
                    break;
            }
            Init_Integer(D_OUT, len);
            return R_OUT; }

        case SYM_CONTEXT: {
            if (Get_Context_Of(D_OUT, val))
                return R_OUT;
            return R_BLANK; }

        default:
            break;
        }

        break; }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(val), action));
}
