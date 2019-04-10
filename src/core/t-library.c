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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  CT_Library: C
//
REBINT CT_Library(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    if (mode >= 0) {
        return VAL_LIBRARY(a) == VAL_LIBRARY(b);
    }
    return -1;
}


//
//  MAKE_Library: C
//
REB_R MAKE_Library(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_LIBRARY);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (!IS_FILE(arg))
        fail (Error_Unexpected_Type(REB_FILE, VAL_TYPE(arg)));

    void *fd = OS_OPEN_LIBRARY(arg);

    if (fd == NULL)
        fail (Error_Bad_Make(REB_LIBRARY, arg));

    REBARR *singular = Alloc_Singular(NODE_FLAG_MANAGED);
    RESET_CELL(ARR_SINGLE(singular), REB_LIBRARY, CELL_MASK_NONE);
    VAL_LIBRARY_SINGULAR_NODE(ARR_SINGLE(singular)) = NOD(singular);

    LINK(singular).fd = fd;
    MISC_META_NODE(singular) = nullptr;  // !!! build from spec, e.g. arg?

    return Move_Value(out, KNOWN(ARR_HEAD(singular)));
}


//
//  TO_Library: C
//
REB_R TO_Library(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Library(out, kind, nullptr, arg);
}


//
//  MF_Library: C
//
void MF_Library(REB_MOLD *mo, const REBCEL *v, bool form)
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
    switch (VAL_WORD_SYM(verb)) {
    case SYM_CLOSE: {
        INCLUDE_PARAMS_OF_CLOSE;

        REBVAL *lib = ARG(port); // !!! generic arg name is "port"?

        if (VAL_LIBRARY_FD(lib) == NULL) {
            // allow to CLOSE an already closed library
        }
        else {
            OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
            LINK(VAL_LIBRARY(lib)).fd = NULL;
        }
        return nullptr; }

    default:
        break;
    }

    return R_UNHANDLED;
}
