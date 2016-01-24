/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2014 Atronix Engineering, Inc.
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
**  Module:  t-library.c
**  Summary: External Library Support
**  Section: datatypes
**  Author:  Shixin Zeng
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

// !!! Why is there a "LIB_POOL"?  Does that really need optimization?
//
#include "mem-pools.h" // low-level memory pool access

//
//  CT_Library: C
//
REBINT CT_Library(const REBVAL *a, const REBVAL *b, REBINT mode)
{
    //RL_Print("%s, %d\n", __func__, __LINE__);
    if (mode >= 0) {
        return VAL_LIB_HANDLE(a) == VAL_LIB_HANDLE(b);
    }
    return -1;
}

//
//  REBTYPE: C
//
REBTYPE(Library)
{
    REBVAL *val = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    // unary actions
    switch(action) {
        case A_MAKE:
            //RL_Print("%s, %d, Make library action\n", __func__, __LINE__);
        case A_TO:
            if (!IS_DATATYPE(val)) {
                fail (Error_Unexpected_Type(REB_LIBRARY, VAL_TYPE(val)));
            }
            if (!IS_FILE(arg)) {
                fail (Error_Unexpected_Type(REB_FILE, VAL_TYPE(arg)));
            } else {
                void *lib = NULL;
                REBCNT error = 0;
                REBSER *path = Value_To_OS_Path(arg, FALSE);
                lib = OS_OPEN_LIBRARY(SER_HEAD(REBCHR, path), &error);
                Free_Series(path);
                if (!lib)
                    fail (Error_Bad_Make(REB_LIBRARY, arg));

                VAL_LIB_SPEC(D_OUT) = Make_Array(1);
                MANAGE_ARRAY(VAL_LIB_SPEC(D_OUT));

                Append_Value(VAL_LIB_SPEC(D_OUT), arg);
                VAL_LIB_HANDLE(D_OUT) = (REBLHL*)Make_Node(LIB_POOL);
                VAL_LIB_FD(D_OUT) = lib;
                USE_LIB(VAL_LIB_HANDLE(D_OUT));
                OPEN_LIB(VAL_LIB_HANDLE(D_OUT));
                VAL_RESET_HEADER(D_OUT, REB_LIBRARY);
            }
            break;
        case A_CLOSE:
            OS_CLOSE_LIBRARY(VAL_LIB_FD(val));
            CLOSE_LIB(VAL_LIB_HANDLE(val));
            SET_UNSET(D_OUT);
            break;
        default:
            fail (Error_Illegal_Action(REB_LIBRARY, action));
    }
    return R_OUT;
}
