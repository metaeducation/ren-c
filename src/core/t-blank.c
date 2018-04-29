//
//  File: %t-blank.c
//  Summary: "Blank datatype"
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
//  CT_Unit: C
//
REBINT CT_Unit(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode >= 0) return (VAL_TYPE(a) == VAL_TYPE(b));
    return -1;
}


//
//  MAKE_Unit: C
//
void MAKE_Unit(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    UNUSED(arg);
    RESET_VAL_HEADER(out, kind);
}


//
//  TO_Unit: C
//
void TO_Unit(REBVAL *out, enum Reb_Kind kind, const REBVAL *data) {
    UNUSED(data);
    RESET_VAL_HEADER(out, kind);
}


//
//  MF_Unit: C
//
void MF_Unit(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    UNUSED(form); // no distinction between MOLD and FORM

    switch (VAL_TYPE(v)) {
    case REB_BAR:
        Append_Unencoded(mo->series, "|");
        break;

    case REB_LIT_BAR:
        Append_Unencoded(mo->series, "'|");
        break;

    case REB_BLANK:
        Append_Unencoded(mo->series, "_");
        break;

    default:
        panic (v);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Unit)
{
    REBVAL *val = D_ARG(1);
    assert(not IS_VOID(val));

    switch (verb) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `val` above
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_TAIL_Q:
            return R_TRUE;

        case SYM_INDEX:
        case SYM_LENGTH:
            return R_BLANK;

        default:
            break;
        }

        break; }

    case SYM_SELECT:
        return R_VOID;

    case SYM_FIND:
        return R_BLANK;

    case SYM_COPY: {
        if (IS_BLANK(val))
            return R_BLANK; // perhaps allow COPY on any type, as well.
        break; }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(val), verb));
}


//
//  CT_Handle: C
//
REBINT CT_Handle(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    // Would it be meaningful to allow user code to compare HANDLE!?
    //
    UNUSED(a);
    UNUSED(b);
    UNUSED(mode);

    fail ("Currently comparing HANDLE! types is not allowed.");
}


//
//  MF_Handle: C
//
void MF_Handle(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    // Value has no printable form, so just print its name.

    if (form)
        Emit(mo, "?T?", v);
    else
        Emit(mo, "+T", v);
}


//
// REBTYPE: C
//
REBTYPE(Handle)
{
    UNUSED(frame_);

    fail (Error_Illegal_Action(REB_HANDLE, verb));
}
