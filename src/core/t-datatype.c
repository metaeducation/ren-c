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
Source* Startup_Datatypes(Array* boot_typespecs)
{
    if (Array_Len(boot_typespecs) != REB_MAX - 1)  // exclude REB_0
        panic (boot_typespecs);

    Source* catalog = Make_Source(REB_MAX - 1);

    REBINT n = 1;

    for (; n < REB_MAX; ++n) {
        Kind kind = cast(Kind, n);

        // Many places in the system want to be able to just off-the-cuff
        // refer to a built-in datatype, without allocating a cell to
        // initialize.  This is done with Datatype_From_Kind().
        //
        // Things like INTEGER! are defined to be &INTEGER
        //
        SymId datatype_sym = cast(SymId, REB_MAX + ((n - 1) * 2) + 1);
        Element* datatype = cast(Element*, Sink_Lib_Var(datatype_sym));
        Protect_Cell(Init_Builtin_Datatype(datatype, kind));
        assert(datatype == Datatype_From_Kind(kind));

        // Things like INTEGER? are fast typechecking "intrinsics".  At one
        // point these were constructed in the mezzanine, but it's faster and
        // less error prone to just make them here.
        //
        SymId constraint_sym = cast(SymId, REB_MAX + ((n - 1) * 2));
        Phase* typechecker = Make_Decider_Intrinsic(kind);
        Init_Action(
            Sink_Lib_Var(constraint_sym),
            typechecker,
            Canon_Symbol(constraint_sym),  // cached symbol for function
            UNBOUND
        );

        // The "catalog of types" could be generated on demand by the system
        // instead of collected and put in the global context.
        //
        Value* word = Init_Any_Word(
            Alloc_Tail_Array(catalog),
            REB_WORD,
            Canon_Symbol(datatype_sym)
        );
        Tweak_Cell_Word_Index(word, INDEX_PATCHED);
        BINDING(word) = &g_lib_patches[datatype_sym];
    }

    return catalog;
}


//
//  Shutdown_Datatypes: C
//
void Shutdown_Datatypes(void)
{
}
