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
    VAL_RESET_HEADER(out, kind);
}


//
//  TO_Unit: C
//
void TO_Unit(REBVAL *out, enum Reb_Kind kind, const REBVAL *data) {
    UNUSED(data);
    VAL_RESET_HEADER(out, kind);
}


//
//  REBTYPE: C
//
REBTYPE(Unit)
{
    REBVAL *val = D_ARG(1);
    assert(!IS_VOID(val));

    switch (action) {
    case SYM_TAIL_Q:
        return R_TRUE;

    case SYM_INDEX_OF:
    case SYM_LENGTH:
    case SYM_SELECT:
    case SYM_FIND:
    case SYM_REMOVE:
    case SYM_CLEAR:
    case SYM_TAKE:
        return R_BLANK;

    case SYM_COPY: {
        if (IS_BLANK(val))
            return R_BLANK; // perhaps allow COPY on any type, as well.
        break; }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(val), action));
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

    fail (Error_Misc_Raw());
}



//
// REBTYPE: C
//
REBTYPE(Handle)
{
    UNUSED(frame_);

    fail (Error_Illegal_Action(REB_HANDLE, action));
}
