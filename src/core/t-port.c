//
//  File: %t-port.c
//  Summary: "port datatype"
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
//  CT_Port: C
//
REBINT CT_Port(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    return VAL_CONTEXT(a) == VAL_CONTEXT(b);
}


//
//  MAKE_Port: C
//
// Create a new port. This is done by calling the MAKE_PORT
// function stored in the system/intrinsic object.
//
void MAKE_Port(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (Apply_Only_Throws(
        out, TRUE, Sys_Func(SYS_CTX_MAKE_PORT_P), arg, END_CELL
    )) {
        // Gave back an unhandled RETURN, BREAK, CONTINUE, etc...
        fail (Error_No_Catch_For_Throw(out));
    }

    // !!! Shouldn't this be testing for !IS_PORT( ) ?
    if (IS_BLANK(out))
        fail (Error(RE_INVALID_SPEC, arg));
}


//
//  TO_Port: C
//
void TO_Port(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (!IS_OBJECT(arg))
        fail (Error_Bad_Make(REB_PORT, arg));

    // !!! cannot convert TO a PORT! without copying the whole context...
    // which raises the question of why convert an object to a port,
    // vs. making it as a port to begin with (?)  Look into why
    // system/standard/port is made with CONTEXT and not with MAKE PORT!
    //
    REBCTX *context = Copy_Context_Shallow(VAL_CONTEXT(arg));
    VAL_RESET_HEADER(CTX_VALUE(context), REB_PORT);
    Val_Init_Port(out, context);
}


//
//  REBTYPE: C
//
REBTYPE(Port)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (action) {

    case SYM_READ:
    case SYM_WRITE:
    case SYM_QUERY:
    case SYM_OPEN:
    case SYM_CREATE:
    case SYM_DELETE:
    case SYM_RENAME:
        // !!! We are going to "re-apply" the call frame with routines that
        // are going to read the D_ARG(1) slot *implicitly* regardless of
        // what value points to.  And dodgily, we must also make sure the
        // output is set.  Review.
        //
        if (!IS_PORT(value)) {
            MAKE_Port(D_OUT, REB_PORT, value);
            *D_ARG(1) = *D_OUT;
            value = D_ARG(1);
        } else
            *D_OUT = *value;
    case SYM_UPDATE:
        break;

    case SYM_REFLECT:
        return T_Context(frame_, action);
    }

    return Do_Port_Action(frame_, VAL_CONTEXT(value), action);
}
