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
// Copyright 2012-2016 Rebol Open Source Contributors
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
        *out = *arg;
        VAL_SET_TYPE_BITS(out, kind);
        return;
    }

    REBSTR *name;

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

        if (kind == REB_ISSUE)
            name = Scan_Issue(bp, len);
        else
            name = Scan_Word(bp, len);

        if (name == NULL)
            fail (Error(RE_BAD_CHAR, arg));
    }
    else if (IS_CHAR(arg)) {
        REBYTE buf[8];
        REBCNT len = Encode_UTF8_Char(&buf[0], VAL_CHAR(arg));
        name = Scan_Word(&buf[0], len);
        if (name == NULL) fail (Error(RE_BAD_CHAR, arg));
    }
    else if (IS_DATATYPE(arg)) {
    #if defined(NDEBUG)
        name = Canon(VAL_TYPE_SYM(arg));
    #else
        if (
            LEGACY(OPTIONS_PAREN_INSTEAD_OF_GROUP)
            && VAL_TYPE_KIND(arg) == REB_GROUP
        ) {
            name = Canon(SYM_PAREN_X); // e_Xclamation point (PAREN!)
        }
        else
            name = Canon(VAL_TYPE_SYM(arg));
    #endif
    }
    else if (IS_LOGIC(arg)) {
        name = VAL_LOGIC(arg) ? Canon(SYM_TRUE) : Canon(SYM_FALSE);
    }
    else
        fail (Error_Unexpected_Type(REB_WORD, VAL_TYPE(arg)));

    Val_Init_Word(out, kind, name);
}


//
//  TO_Word: C
//
void TO_Word(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Word(out, kind, arg);
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
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (action) {
    case SYM_LENGTH: {
        const REBYTE *bp = STR_HEAD(VAL_WORD_SPELLING(val));
        REBCNT len = 0;
        while (TRUE) {
            REBUNI ch;
            if (!(bp = Back_Scan_UTF8_Char(&ch, bp, &len)))
                fail(Error(RE_BAD_UTF8));
            if (ch == 0)
                break;
        }
        SET_INTEGER(D_OUT, len);
        return R_OUT; }

    default:
        assert(ANY_WORD(val));
        fail (Error_Illegal_Action(VAL_TYPE(val), action));
    }

    return R_OUT;
}
