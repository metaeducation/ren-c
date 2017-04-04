//
//  File: %t-library.c
//  Summary: "External Library Support"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2017 Rebol Open Source Contributors
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
//  CT_Library: C
//
REBINT CT_Library(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode >= 0) {
        return VAL_LIBRARY(a) == VAL_LIBRARY(b);
    }
    return -1;
}


//
//  MAKE_Library: C
//
void MAKE_Library(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_LIBRARY);
    UNUSED(kind);

    if (!IS_FILE(arg))
        fail (Error_Unexpected_Type(REB_FILE, VAL_TYPE(arg)));

    REBCNT error = 0;

    REBSER *path = Value_To_OS_Path(arg, FALSE);
    void *fd = OS_OPEN_LIBRARY(SER_HEAD(REBCHR, path), &error);
    Free_Series(path);

    if (!fd)
        fail (Error_Bad_Make(REB_LIBRARY, arg));

    REBARR *singular = Alloc_Singular_Array();
    Reset_Val_Header(ARR_HEAD(singular), REB_LIBRARY);
    ARR_HEAD(singular)->payload.library.singular = singular;

    AS_SERIES(singular)->misc.fd = fd;
    AS_SERIES(singular)->link.meta = NULL; // build from spec, e.g. arg?

    MANAGE_ARRAY(singular);
    Move_Value(out, KNOWN(ARR_HEAD(singular)));
}


//
//  TO_Library: C
//
void TO_Library(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Library(out, kind, arg);
}


//
//  REBTYPE: C
//
REBTYPE(Library)
{
    switch(action) {
    case SYM_CLOSE: {
        INCLUDE_PARAMS_OF_CLOSE;

        REBVAL *lib = ARG(port); // !!! generic arg name is "port"?

        if (VAL_LIBRARY_FD(lib) == NULL) {
            // allow to CLOSE an already closed library
        }
        else {
            OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
            AS_SERIES(VAL_LIBRARY(lib))->misc.fd = NULL;
        }
        return R_VOID; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_LIBRARY, action));
}
