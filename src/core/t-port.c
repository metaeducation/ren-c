//
//  File: %t-port.c
//  Summary: "port datatype"
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
//  CT_Port: C
//
REBINT CT_Port(REBCEL(const*) a, REBCEL(const*) b, REBINT strict)
{
    UNUSED(strict);
    if (VAL_CONTEXT(a) == VAL_CONTEXT(b))
        return 0;
    return VAL_CONTEXT(a) > VAL_CONTEXT(b) ? 1 : -1;  // !!! Review
}


//
//  MAKE_Port: C
//
// Create a new port. This is done by calling the MAKE_PORT
// function stored in the system/intrinsic object.
//
REB_R MAKE_Port(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_PORT);
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    const bool fully = true; // error if not all arguments consumed

    assert(not IS_NULLED(arg)); // would need to DEVOID it otherwise
    if (rebRunThrows(out, fully, Sys(SYM_MAKE_PORT_P), rebQ(arg), rebEND))
        fail (Error_No_Catch_For_Throw(out));

    // !!! Shouldn't this be testing for !IS_PORT( ) ?
    if (IS_BLANK(out))
        fail (Error_Invalid_Spec_Raw(arg));

    return out;
}


//
//  TO_Port: C
//
REB_R TO_Port(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_PORT);
    UNUSED(kind);

    if (!IS_OBJECT(arg))
        fail (Error_Bad_Make(REB_PORT, arg));

    // !!! cannot convert TO a PORT! without copying the whole context...
    // which raises the question of why convert an object to a port,
    // vs. making it as a port to begin with (?)  Look into why
    // system/standard/port is made with CONTEXT and not with MAKE PORT!
    //
    REBCTX *context = Copy_Context_Shallow_Managed(VAL_CONTEXT(arg));
    RESET_VAL_HEADER(
        CTX_ROOTVAR(context),
        REB_PORT,
        CELL_MASK_CONTEXT
    );

    return Init_Port(out, context);
}


//
//  Retrigger_Append_As_Write: C
//
// !!! In R3-Alpha, for the convenience of being able to APPEND to something
// that may be a FILE!-based PORT! or a BINARY! or STRING! with a unified
// interface, the APPEND command was re-interpreted as a WRITE/APPEND.  But
// it was done with presumption that APPEND and WRITE had compatible frames,
// which generally speaking they do not.
//
// This moves the functionality to an actual retriggering which calls whatever
// WRITE/APPEND would do in a generic fashion with a new frame.  Not all
// ports do this, as some have their own interpretation of APPEND.  It's
// hacky, but still not as bad as it was.  Review.
//
REB_R Retrigger_Append_As_Write(REBFRM *frame_) {
    INCLUDE_PARAMS_OF_APPEND;

    // !!! Something like `write/append %foo.txt "data"` knows to convert
    // %foo.txt to a port before trying the write, but if you say
    // `append %foo.txt "data"` you get `%foo.txtdata`.  Some actions are like
    // this, e.g. PICK, where they can't do the automatic conversion.
    //
    assert(IS_PORT(ARG(series))); // !!! poorly named
    UNUSED(ARG(series));
    if (not (
        IS_BINARY(ARG(value))
        or IS_TEXT(ARG(value))
        or IS_BLOCK(ARG(value)))
    ){
        fail (PAR(value));
    }

    if (REF(part) or REF(dup) or REF(line))
        fail (Error_Bad_Refines_Raw());

    return rebValue("write/append @", D_ARG(1), "@", D_ARG(2));
}


//
//  REBTYPE: C
//
// !!! The concept of port dispatch from R3-Alpha is that it delegates to a
// handler which may be native code or user code.
//
REBTYPE(Port)
{
    // !!! The ability to transform some BLOCK!s into PORT!s for some actions
    // was hardcoded in a fairly ad-hoc way in R3-Alpha, which was based on
    // an integer range of action numbers.  Ren-C turned these numbers into
    // symbols, where order no longer applied.  The mechanism needs to be
    // rethought, see:
    //
    // https://github.com/metaeducation/ren-c/issues/311
    //
    if (not IS_PORT(D_ARG(1))) {
        switch (VAL_WORD_ID(verb)) {

        case SYM_READ:
        case SYM_WRITE:
        case SYM_QUERY:
        case SYM_OPEN:
        case SYM_CREATE:
        case SYM_DELETE:
        case SYM_RENAME: {
            //
            // !!! We are going to "re-apply" the call frame with routines we
            // are going to read the D_ARG(1) slot *implicitly* regardless of
            // what value points to.
            //
            const REBVAL *made = rebValue("make port! @", D_ARG(1));
            assert(IS_PORT(made));
            Copy_Cell(D_ARG(1), made);
            rebRelease(made);
            break; }

        case SYM_ON_WAKE_UP:
            break;

        // Once handled SYM_REFLECT here by delegating to T_Context(), but
        // common reflectors now in Context_Common_Action_Or_End()

        default:
            break;
        }
    }

    if (not IS_PORT(D_ARG(1)))
        fail (D_ARG(1));

    REBVAL *port = D_ARG(1);

    REB_R r = Context_Common_Action_Maybe_Unhandled(frame_, verb);
    if (r != R_UNHANDLED)
        return r;

    return Do_Port_Action(frame_, port, verb);
}
