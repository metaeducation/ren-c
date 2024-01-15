//
//  File: %cell-context.h
//  Summary: {Context definitions AFTER including %tmp-internals.h}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// Errors are a subtype of ANY-CONTEXT! which follow a standard layout.
// That layout is in %boot/sysobj.r as standard/error.
//
// Historically errors could have a maximum of 3 arguments, with the fixed
// names of `arg1`, `arg2`, and `arg3`.  They would also have a numeric code
// which would be used to look up a a formatting block, which would contain
// a block for a message with spots showing where the args were to be inserted
// into a message.  These message templates can be found in %boot/errors.r
//
// Ren-C is exploring the customization of user errors to be able to provide
// arbitrary named arguments and message templates to use them.  It is
// a work in progress, but refer to the FAIL native, the corresponding
// `fail()` C macro in the source, and the various routines in %c-error.c
//

#define ERR_VARS(e) \
    cast(ERROR_VARS*, CTX_VARS_HEAD(e))

#define VAL_ERR_VARS(v) \
    ERR_VARS(VAL_CONTEXT(v))

#define Init_Error(v,c) \
    Init_Context_Cell((v), REB_ERROR, (c))

INLINE void Force_Location_Of_Error(Context* error, Level* where) {
    ERROR_VARS *vars = ERR_VARS(error);
    if (Is_Nulled(&vars->where))
        Set_Location_Of_Error(error, where);
}


// An antiform ERROR! represents a thrown state.  This failure state can't be
// stored in variables and will raise an alarm if something in a processing
// pipeline doesn't ask to ^META it.  While it's in the ^META state it can
// also be passed around normally until it's UNMETA'd back to a failure again.

INLINE bool Is_Raised(Atom(const*) v)
  { return HEART_BYTE(v) == REB_ERROR and QUOTE_BYTE(v) == ANTIFORM_0; }

INLINE Atom(*) Raisify(Atom(*) v) {
    assert(Is_Error(v) and QUOTE_BYTE(v) == NOQUOTE_1);
    Force_Location_Of_Error(VAL_CONTEXT(v), TOP_LEVEL);  // ideally already set
    QUOTE_BYTE(v) = ANTIFORM_0;
    return v;
}

#if CPLUSPLUS_11
    void Is_Raised(Value(const*) v) = delete;
    void Raisify(Value(*) v) = delete;
#endif

INLINE bool Is_Meta_Of_Raised(const Cell* v)
  { return HEART_BYTE(v) == REB_ERROR and QUOTE_BYTE(v) == QUASIFORM_2; }
