//
//  file: %cell-context.h
//  summary: "Context definitions AFTER including %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// An FAILURE! in Ren-C is an antiform error state.
//
// It is specifically an "unstable antiform"...which means it not only can't
// be stored in lists like BLOCK!...it also can't be stored in variables.
// Failures will be elevated to an exceptions if you try to manipulate them
// without using ^META operations.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//


//=//// ERROR FIELD ACCESS ////////////////////////////////////////////////=//
//
// Errors are a subtype of ANY-CONTEXT? which follow a standard layout.
// That layout is in %specs/sysobj.r as standard/error.
//
// Historically errors could have a maximum of 3 arguments, with the fixed
// names of `arg1`, `arg2`, and `arg3`.  They would also have a numeric code
// which would be used to look up a a formatting block, which would contain
// a block for a message with spots showing where the args were to be inserted
// into a message.  These message templates can be found in %specs/errors.r
//
// Ren-C is exploring the customization of user errors to be able to provide
// arbitrary named arguments and message templates to use them.  It is
// a work in progress, but refer to the FAIL native, the corresponding
// `panic()` C macro in the source, and the various routines in %c-error.c

#define ERR_VARS(e) \
    cast(ERROR_VARS*, Varlist_Slots_Head(e))

#define VAL_ERR_VARS(v) \
    ERR_VARS(Cell_Varlist(v))

INLINE void Force_Location_Of_Error(Error* error, Level* L) {
    ERROR_VARS *vars = ERR_VARS(error);

    DECLARE_STABLE (where);
    require (
      Read_Slot(where, &vars->where)
    );
    if (Is_Null(where))
        Set_Location_Of_Error(error, L);
}


//=//// NON-ANTIFORM ERROR! STATE /////////////////////////////////////////=//
//
// It may be that FAILURE! becomes simply an antiform of any generic OBJECT!
// (a bit like "armed" errors vs. "disarmed" objects in Rebol2).
//
// However, the ERROR! type has historically been a specially formatted
// subtype of OBJECT!.
//

INLINE Value* Failify_Cell_And_Force_Location(Exact(Value*) v) {
    assert(Is_Possibly_Unstable_Value_Error(v));
    Force_Location_Of_Error(Cell_Error(v), TOP_LEVEL);  // ideally a noop
    Unstably_Antiformize_Unbound_Fundamental(v);
    assert(Is_Failure(v));
    return v;  // ERROR! => FAILURE!
}

INLINE Element* Disarm_Failure(Exact(Value*) v) {  // FAILURE! => ERROR!
    assert(Is_Failure(v));
    LIFT_BYTE(v) = NOQUOTE_3;
    assert(Is_Possibly_Unstable_Value_Error(v));
    return As_Element(v);
}
