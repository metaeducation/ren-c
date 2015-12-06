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
**  Module:  t-port.c
**  Summary: port datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


//
//  CT_Port: C
//
REBINT CT_Port(REBVAL *a, REBVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    return VAL_FRAME(a) == VAL_FRAME(b);
}


//
//  MT_Port: C
//
REBFLG MT_Port(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
    return FALSE;
}


//
//  REBTYPE: C
//
REBTYPE(Port)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBFRM *frame;

    switch (action) {

    case A_READ:
    case A_WRITE:
    case A_QUERY:
    case A_OPEN:
    case A_CREATE:
    case A_DELETE:
    case A_RENAME:
        // !!! We are going to "re-apply" the call frame with routines that
        // are going to read the D_ARG(1) slot *implicitly* regardless of
        // what value points to.  And dodgily, we must also make sure the
        // output is set.  Review.
        //
        if (!IS_PORT(value)) {
            Make_Port(D_OUT, value);
            *D_ARG(1) = *D_OUT;
            value = D_ARG(1);
        } else
            *D_OUT = *value;
    case A_UPDATE:
        break;

    case A_REFLECT:
        return T_Object(call_, action);

    case A_MAKE:
        if (!IS_DATATYPE(value)) fail (Error_Bad_Make(REB_PORT, value));
        Make_Port(D_OUT, arg);
        return R_OUT;

    case A_TO:
        if (!(IS_DATATYPE(value) && IS_OBJECT(arg)))
            fail (Error_Bad_Make(REB_PORT, arg));

        // !!! cannot convert TO a PORT! without copying the whole frame...
        // which raises the question of why convert an object to a port,
        // vs. making it as a port to begin with (?)  Look into why
        // system/standard/port is made with CONTEXT and not with MAKE PORT!
        //
        frame = Copy_Frame_Shallow_Managed(VAL_FRAME(arg));
        VAL_SET(FRAME_CONTEXT(frame), REB_PORT);
        Val_Init_Port(D_OUT, frame);
        return R_OUT;
    }

    return Do_Port_Action(call_, VAL_FRAME(value), action);
}
