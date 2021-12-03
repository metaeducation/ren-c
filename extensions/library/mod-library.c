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
// Copyright 2014-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"

#include "sys-library.h"

#include "tmp-mod-library.h"


REBTYP *EG_Library_Type = nullptr;  // (E)xtension (G)lobal LIBRARY! type


//
//  CT_Library: C
//
REBINT CT_Library(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);
    return VAL_LIBRARY(a) == VAL_LIBRARY(b);
}


//
//  MAKE_Library: C
//
REB_R MAKE_Library(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_CUSTOM);

    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    if (not IS_FILE(arg))
        fail (Error_Unexpected_Type(REB_FILE, VAL_TYPE(arg)));

    void *fd = Open_Library(arg);

    if (fd == NULL)
        fail (arg);

    REBLIB *lib = Alloc_Singular(FLAG_FLAVOR(LIBRARY) | NODE_FLAG_MANAGED);
    Init_Trash(ARR_SINGLE(lib));  // !!! save name? other data?

    lib->link.fd = fd;  // seen as shared by all instances
    node_MISC(Meta, lib) = nullptr;  // !!! build from spec, e.g. arg?

    RESET_CUSTOM_CELL(out, EG_Library_Type, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE1(out, lib);

    return out;
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
void MF_Library(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    End_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Library)
{
    switch (ID_OF_SYMBOL(verb)) {
    case SYM_CLOSE: {
        INCLUDE_PARAMS_OF_CLOSE;

        REBVAL *lib = ARG(port); // !!! generic arg name is "port"?

        if (VAL_LIBRARY_FD(lib) == NULL) {
            // allow to CLOSE an already closed library
        }
        else {
            Close_Library(VAL_LIBRARY_FD(lib));
            VAL_LIBRARY(lib)->link.fd = nullptr;
        }
        return nullptr; }

    default:
        break;
    }

    return R_UNHANDLED;
}


//
//  startup*: native [
//
//  {Register the LIBRARY! datatype (so MAKE LIBRARY! [] etc. work)}
//
//      return: <none>
//  ]
//
REBNATIVE(startup_p)
{
    LIBRARY_INCLUDE_PARAMS_OF_STARTUP_P;

    // !!! See notes on Hook_Datatype for this poor-man's substitute for a
    // coherent design of an extensible object system (as per Lisp's CLOS)
    //
    EG_Library_Type = Hook_Datatype(
        "http://datatypes.rebol.info/library",
        "external library reference",
        &T_Library,
        &PD_Fail,
        &CT_Library,
        &MAKE_Library,
        &TO_Library,
        &MF_Library
    );

    Extend_Generics_Someday(nullptr);  // !!! See comments

    return Init_None(D_OUT);
}


extern RL_LIB Ext_Lib;

//
//  export run-library-collator: native [
//
//  {Execute a function in a DLL or other library that returns a REBVAL*}
//
//      return: [<opt> any-value!]
//      library [library!]
//      linkname [text!]
//  ]
//
REBNATIVE(run_library_collator)
{
    LIBRARY_INCLUDE_PARAMS_OF_RUN_LIBRARY_COLLATOR;

    // !!! This code used to check for loading an already loaded
    // extension.  It looked in an "extensions list", but now that the
    // extensions are modules really this should just be the same as
    // looking in the modules list.  Such code should be in usermode
    // (very awkward in C).  The only unusual C bit was:
    //
    //     // found the existing extension, decrease the reference
    //     // added by MAKE_library
    //     //
    //     OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
    //

    CFUNC *cfunc = Find_Function(
        VAL_LIBRARY_FD(ARG(library)),
        cs_cast(STR_HEAD(VAL_STRING(ARG(linkname))))
    );
    if (cfunc == nullptr)
        fail ("Could not find collator function in library");

    COLLATE_CFUNC *collator = cast(COLLATE_CFUNC*, cfunc);

    // We pass the collation entry point the table of API functions.  This is
    // how DLLs know the addresses of functions in the EXE that they can call.
    // If the extension is built in with the executable, it uses a shortcut
    // and just calls the RL_rebXXX() functions directly...so it does not use
    // the table we're passing.
    //
    return collator(&Ext_Lib);
}


//
//  shutdown_p: native [
//
//  {Unregister the LIBRARY! datatype (MAKE LIBRARY! will fail)}
//
//      return: <none>
//  ]
//
REBNATIVE(shutdown_p)
{
    LIBRARY_INCLUDE_PARAMS_OF_SHUTDOWN_P;

    Unhook_Datatype(EG_Library_Type);
    EG_Library_Type = nullptr;

    return Init_None(D_OUT);
}
