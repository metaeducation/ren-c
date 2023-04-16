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
Array(*) Startup_Datatypes(Array(*) boot_typespecs)
{
    if (ARR_LEN(boot_typespecs) != REB_MAX - 1)  // exclude REB_VOID
        panic (boot_typespecs);

    Array(*) catalog = Make_Array(REB_MAX - 1);

    REBINT n = 1;

    for (; n < REB_MAX; ++n) {
        enum Reb_Kind kind = cast(enum Reb_Kind, n);

        // Many places in the system want to be able to just off-the-cuff
        // refer to a built-in datatype, without allocating a cell to
        // initialize.  This is done with Datatype_From_Kind().  Once upon
        // a time the Lib definitions like INTEGER! would hold a DATATYPE!
        // but now that INTEGER! is a type constraint, it would have to be
        // held by another variable like plain INTEGER...but this competes
        // with things like BLANK wanting to be a literal _ blank.  So we
        // just put a global table off to the side in `Datatypes`.
        //
        Init_Builtin_Datatype(&Datatypes[n], kind);
        assert(&Datatypes[n] == Datatype_From_Kind(kind));
        Set_Cell_Flag(&Datatypes[n], PROTECTED);

        // As an interim step, things like INTEGER! are defined to be &INTEGER!
        // This is being worked on, so that INTEGER! is a type constraint, but
        // it's a lot of work.
        //
        SymId constraint_sym = cast(SymId, REB_MAX + (n - 1) * 2 + 1);
        Value(*) value = Force_Lib_Var(constraint_sym);
        Init_Any_Word(value, REB_TYPE_WORD, Canon_Symbol(SYM_FROM_KIND(kind)));
        Set_Cell_Flag(value, PROTECTED);

        // The "catalog of types" is somewhere that could serve as Datatypes[]
        // if that is reconsidered.
        //
        Init_Any_Word_Bound(
            Alloc_Tail_Array(catalog),
            REB_WORD,
            Canon_Symbol(constraint_sym),
            Lib_Context,
            INDEX_ATTACHED
        );
    }

    return catalog;
}


//
//  Shutdown_Datatypes: C
//
void Shutdown_Datatypes(void)
{
    REBINT n = 1;

    for (; n < REB_MAX; ++n) {
        Erase_Cell(&Datatypes[n]);
    }
}
