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
REBINT CT_Word(const Cell* a, const Cell* b, REBINT mode)
{
    REBINT e;
    REBINT diff;
    if (mode >= 0) {
        if (mode == 1) {
            //
            // Symbols must be exact match, case-sensitively
            //
            if (Cell_Word_Symbol(a) != Cell_Word_Symbol(b))
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
REB_R MAKE_Word(Value* out, enum Reb_Kind kind, const Value* arg)
{
    if (ANY_WORD(arg)) {
        //
        // Only reset the type, not all the header bits (the bits must
        // stay in sync with the binding state)
        //
        Move_Value(out, arg);
        CHANGE_VAL_TYPE_BITS(out, kind);
        return out;
    }

    if (ANY_STRING(arg)) {
        REBSIZ size;
        REBYTE *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_WORD);

        if (kind == REB_ISSUE) {
            if (nullptr == Scan_Issue(out, bp, size))
                fail (Error_Bad_Char_Raw(arg));
        }
        else {
            if (nullptr == Scan_Any_Word(out, kind, bp, size))
                fail (Error_Bad_Char_Raw(arg));
        }
        return out;
    }
    else if (IS_CHAR(arg)) {
        REBYTE buf[8];
        REBLEN len = Encode_UTF8_Char(&buf[0], VAL_CHAR(arg));
        if (nullptr == Scan_Any_Word(out, kind, &buf[0], len))
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
REB_R TO_Word(Value* out, enum Reb_Kind kind, const Value* arg)
{
    return MAKE_Word(out, kind, arg);
}


//
//  MF_Word: C
//
void MF_Word(REB_MOLD *mo, const Cell* v, bool form) {
    UNUSED(form); // no difference between MOLD and FORM at this time

    Symbol* symbol = Cell_Word_Symbol(v);
    const char *head = STR_HEAD(symbol);  // UTF-8
    size_t size = STR_SIZE(symbol);  // number of UTF-8 bytes

    REBSER *s = mo->series;

    switch (VAL_TYPE(v)) {
    case REB_WORD: {
        Append_Utf8_Utf8(s, head, size);
        break; }

    case REB_SET_WORD:
        Append_Utf8_Utf8(s, head, size);
        Append_Utf8_Codepoint(s, ':');
        break;

    case REB_GET_WORD:
        Append_Utf8_Codepoint(s, ':');
        Append_Utf8_Utf8(s, head, size);
        break;

    case REB_LIT_WORD:
        Append_Utf8_Codepoint(s, '\'');
        Append_Utf8_Utf8(s, head, size);
        break;

    case REB_REFINEMENT:
        Append_Utf8_Codepoint(s, '/');
        Append_Utf8_Utf8(s, head, size);
        break;

    case REB_ISSUE:
        Append_Utf8_Codepoint(s, '#');
        Append_Utf8_Utf8(s, head, size);
        break;

    default:
        panic (v);
    }
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
    const Value* picker,
    const Value* opt_setval
){
    Symbol* str = Cell_Word_Symbol(pvs->out);

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

        return R_UNHANDLED;
    }

    return R_UNHANDLED;
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
    Value* val = D_ARG(1);
    assert(ANY_WORD(val));

    switch (Cell_Word_Id(verb)) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        Option(SymId) property = Cell_Word_Id(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH: {
            Symbol* symbol = Cell_Word_Symbol(val);
            const REBYTE *bp = cb_cast(STR_HEAD(symbol));
            REBSIZ size = STR_SIZE(symbol);
            REBLEN len = 0;
            for (; size > 0; ++bp, --size) {
                if (*bp < 0x80)
                    ++len;
                else {
                    REBUNI uni;
                    if (not (bp = Back_Scan_UTF8_Char(&uni, bp, &size)))
                        fail (Error_Bad_Utf8_Raw());
                    ++len;
               }
            }
            return Init_Integer(D_OUT, len); }

        case SYM_BINDING: {
            if (Did_Get_Binding_Of(D_OUT, val))
                return D_OUT;
            return nullptr; }

        default:
            break;
        }

        break; }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(val), verb));
}
