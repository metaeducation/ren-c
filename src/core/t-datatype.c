//
//  File: %t-datatype.c
//  Summary: "datatype datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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


//
//  Startup_Datatypes: C
//
// Create library words for each type, (e.g. make INTEGER! correspond to
// the integer datatype value).  Returns an array of words for the added
// datatypes to use in SYSTEM/CATALOG/DATATYPES.  See %boot/types.r
//
// 1. Things like INTEGER! are defined to be ~{integer!}~ antiforms.
//
// 2. Many places in the system want to be able to just off-the-cuff refer to
//    a built-in datatype, without allocating a cell to initialize.  This is
//    done with Datatype_From_Type(), that returns it from the lib context.
//
// 3. R3-Alpha had a number of "catalogs" in the global context.  There's no
//    real reason that the "catalog of types" isn't generated on demand by the
//    system instead of collected and put in the global context whether you
//    ever want it or not.
//
Source* Startup_Datatypes(Array* boot_typespecs)
{
    if (Array_Len(boot_typespecs) != MAX_TYPE_BYTE)  // exclude TYPE_0
        panic (boot_typespecs);

    Source* catalog = Make_Source(MAX_TYPE_BYTE);

    Byte n = 1;

    for (; n <= MAX_TYPE_BYTE; ++n) {
        Type type = u_cast(TypeEnum, n);

        SymId datatype_id = cast(SymId, n);  // includes the "!", e.g. integer!
        Element* datatype = cast(Element*, Sink_Lib_Var(datatype_id));
        Protect_Cell(Init_Builtin_Datatype(datatype, type));  // datatype [1]
        assert(datatype == Datatype_From_Type(type));  // convenient [2]
        assert(Cell_Datatype_Type(datatype) == type);  // sanity check

        Element* word = Init_Any_Word(
            Alloc_Tail_Array(catalog),
            TYPE_WORD,
            Canon_Symbol(datatype_id)
        );
        Tweak_Cell_Word_Index(word, INDEX_PATCHED);
        Tweak_Cell_Binding(word, &g_lib_patches[datatype_id]);
    }

    return catalog;  // could be generated on demand [3]
}


//
//  Shutdown_Datatypes: C
//
void Shutdown_Datatypes(void)
{
}
