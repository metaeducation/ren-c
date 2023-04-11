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
Array(*) Startup_Datatypes(Array(*) boot_types, Array(*) boot_typespecs)
{
    UNUSED(boot_typespecs);  // not currently used

    if (ARR_LEN(boot_types) != REB_MAX - 1)  // exclude REB_VOID
        panic (boot_types);  // other types/internals should have a WORD!

    Array(*) catalog = Make_Array(REB_MAX - 1);

    Cell(const*) word_tail = ARR_TAIL(boot_types);
    Cell(*) word = ARR_HEAD(boot_types);

    REBINT n = VAL_WORD_ID(word);
    if (n != SYM_DECIMAL_X)  // first symbol (SYM_NULL is something random)
        panic (word);

    for (; word != word_tail; ++word, ++n) {
        enum Reb_Kind kind = cast(enum Reb_Kind, n);

        assert(Canon_Symbol(cast(SymId, kind)) == VAL_WORD_SYMBOL(word));

        Value(*) value = Force_Lib_Var(cast(SymId, kind));

        // !!! Currently datatypes are just molded specially to look like an
        // ANY-BLOCK! type, so they seem like &[integer] or &['word].  But the
        // idea is that they will someday actually be blocks, so having some
        // read-only copies of the common types remade would save on series
        // allocations.  We pre-build the types into the lib slots in an
        // anticipation of that change.
        //
        Init_Any_Word(value, REB_TYPE_WORD, Canon_Symbol(SYM_FROM_KIND(kind)));

        // !!! The system depends on these definitions, as they are used by
        // Get_Type and Type_Of.  Lock it for safety...though consider an
        // alternative like using the returned types catalog and locking
        // that.  (It would be hard to rewrite lib to safely change a type
        // definition, given the code doing the rewriting would likely depend
        // on lib...but it could still be technically possible, even in
        // a limited sense.)
        //
        assert(value == Datatype_From_Kind(kind));
        Set_Cell_Flag(value, PROTECTED);

        Append_Value(catalog, SPECIFIC(word));
    }

    return catalog;
}


//
//  Shutdown_Datatypes: C
//
void Shutdown_Datatypes(void)
{
}
