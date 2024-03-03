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
REBINT CT_Library(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode >= 0) {
        return VAL_LIBRARY(a) == VAL_LIBRARY(b);
    }
    return -1;
}


//
//  MAKE_Library: C
//
REB_R MAKE_Library(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == REB_LIBRARY);
    UNUSED(kind);

    if (!IS_FILE(arg))
        fail (Error_Unexpected_Type(REB_FILE, VAL_TYPE(arg)));

    void *fd = OS_OPEN_LIBRARY(arg);

    if (fd == nullptr)
        fail (Error_Bad_Make(REB_LIBRARY, arg));

    Array* singular = Alloc_Singular(NODE_FLAG_MANAGED);
    RESET_CELL(ARR_SINGLE(singular), REB_LIBRARY);
    ARR_SINGLE(singular)->payload.library.singular = singular;

    LINK(singular).fd = fd;
    MISC(singular).meta = nullptr;  // build from spec, e.g. arg?

    return Copy_Cell(out, KNOWN(ARR_HEAD(singular)));
}


//
//  TO_Library: C
//
REB_R TO_Library(Value* out, enum Reb_Kind kind, const Value* arg)
{
    return MAKE_Library(out, kind, arg);
}


//
//  MF_Library: C
//
void MF_Library(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    REBCTX *meta = VAL_LIBRARY_META(v);
    if (meta)
        MF_Context(mo, CTX_ARCHETYPE(meta), form);

    End_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Library)
{
    switch (Cell_Word_Id(verb)) {
    case SYM_CLOSE: {
        INCLUDE_PARAMS_OF_CLOSE;

        Value* lib = ARG(port); // !!! generic arg name is "port"?

        if (VAL_LIBRARY_FD(lib) == nullptr) {
            // allow to CLOSE an already closed library
        }
        else {
            OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
            LINK(VAL_LIBRARY(lib)).fd = nullptr;
        }
        return nullptr; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_LIBRARY, verb));
}
