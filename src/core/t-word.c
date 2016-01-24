/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  t-word.c
**  Summary: word related datatypes
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


//
//  CT_Word: C
//
// !!! The R3-Alpha code did a non-ordering comparison; it only tells whether
// the words are equal or not (1 or 0).  This creates bad invariants for
// sorting etc.  Review.
//
REBINT CT_Word(const REBVAL *a, const REBVAL *b, REBINT mode)
{
    REBINT e;
    REBINT diff;
    if (mode >= 0) {
        if (mode >= 2) {
            //
            // Symbols must be exact match, case-sensitively
            //
            if (VAL_WORD_SYM(a) != VAL_WORD_SYM(b))
                return 0;
        }
        else {
            // Different cases acceptable, only check for a canon match
            //
            if (VAL_WORD_CANON(a) != VAL_WORD_CANON(b))
                return 0;
        }

        if (mode >= 1) {
            //
            // Check binding information

            if (VAL_WORD_INDEX(a) != VAL_WORD_INDEX(b))
                return 0;

            if (GET_VAL_FLAG(a, WORD_FLAG_BOUND_SPECIFIC)) {
                if (!GET_VAL_FLAG(b, WORD_FLAG_BOUND_SPECIFIC))
                    return 0;

                return (VAL_WORD_CONTEXT(a) == VAL_WORD_CONTEXT(b)) ? 1 : 0;
            }

            if (GET_VAL_FLAG(a, WORD_FLAG_BOUND_RELATIVE)) {
                if (!GET_VAL_FLAG(b, WORD_FLAG_BOUND_RELATIVE)) {
                    //
                    // !!! We'll need to be able to compare a relative bound
                    // word to a frame bound word... but for the moment
                    // the only guesstimation of equality would be if the
                    // word were equal when considered in the current
                    // stack level, e.g. a stack-relative comparison...and
                    // that broken dependency isn't worth hacking in here.
                    //
                    return 0;
                }

                return (a->payload.any_word.binding.relative
                    == b->payload.any_word.binding.relative) ? 1 : 0;
            }

            // `a` isn't bound, so it matches if `b` is unbound too.

            assert(IS_WORD_UNBOUND(a));
            if (IS_WORD_BOUND(b))
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
//  REBTYPE: C
//
REBTYPE(Word)
{
    REBVAL *val = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    enum Reb_Kind type = VAL_TYPE(val);
    REBINT diff;
    REBSYM sym;

    switch (action) {
    case A_MAKE:
    case A_TO:
        // TO word! ...
        if (type == REB_DATATYPE) type = VAL_TYPE_KIND(val);
        if (ANY_WORD(arg)) {
            //
            // Only reset the type, not all the header bits (the bits must
            // stay in sync with the binding state)
            //
            VAL_SET_TYPE_BITS(arg, type);
            *D_OUT = *D_ARG(2);
            return R_OUT;
        }
        else {
            if (IS_STRING(arg)) {
                REBYTE *bp;
                REBCNT len;
                const REBOOL allow_utf8 = TRUE;

                // Set sym. Rest is set below.  If characters in the source
                // string are > 0x80 they will be encoded to UTF8 to be stored
                // in the symbol.
                //
                bp = Temp_Byte_Chars_May_Fail(
                    arg, MAX_SCAN_WORD, &len, allow_utf8
                );

                if (type == REB_ISSUE) sym = Scan_Issue(bp, len);
                else sym = Scan_Word(bp, len);
                if (!sym) fail (Error(RE_BAD_CHAR, arg));
            }
            else if (IS_CHAR(arg)) {
                REBYTE buf[8];
                sym = Encode_UTF8_Char(&buf[0], VAL_CHAR(arg)); //returns length
                sym = Scan_Word(&buf[0], sym);
                if (!sym) fail (Error(RE_BAD_CHAR, arg));
            }
            else if (IS_DATATYPE(arg)) {
            #if defined(NDEBUG)
                sym = VAL_TYPE_SYM(arg);
            #else
                if (
                    LEGACY(OPTIONS_PAREN_INSTEAD_OF_GROUP)
                    && VAL_TYPE_KIND(arg) == REB_GROUP
                ) {
                    sym = SYM_PAREN_X; // e_Xclamation point (GROUP!)
                }
                else
                    sym = VAL_TYPE_SYM(arg);
            #endif
            }
            else if (IS_LOGIC(arg)) {
                sym = VAL_LOGIC(arg) ? SYM_TRUE : SYM_FALSE;
            }
            else
                fail (Error_Unexpected_Type(REB_WORD, VAL_TYPE(arg)));

            Val_Init_Word(D_OUT, type, sym);
        }
        break;

    default:
        fail (Error_Illegal_Action(type, action));
    }

    return R_OUT;
}
