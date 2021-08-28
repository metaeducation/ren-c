//
//  File: %c-port.c
//  Summary: "support for I/O ports"
//  Section: core
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
// See comments in Init_Ports for startup.
// See www.rebol.net/wiki/Event_System for full details.
//

#include "sys-core.h"


//
//  Is_Port_Pending: C
//
// Return true if port value is pending a signal (informs WAIT, e.g. if not
// pending then WAIT on the port is a no-oip).
//
bool Is_Port_Pending(const RELVAL *port)
{
    assert(IS_PORT(port));

    if (VAL_LOGIC(CTX_VAR(VAL_CONTEXT(port), STD_PORT_PENDING)))
        return true;

    if (not IS_NULLED(CTX_VAR(VAL_CONTEXT(port), STD_PORT_AWAKE)))
        return true;

    return false;
}


//
//  Do_Port_Action: C
//
// Call a PORT actor (action) value. Search PORT actor
// first. If not found, search the PORT scheme actor.
//
// NOTE: stack must already be setup correctly for action, and
// the caller must cleanup the stack.
//
REB_R Do_Port_Action(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
{
    FAIL_IF_BAD_PORT(port);

    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *actor = CTX_VAR(ctx, STD_PORT_ACTOR);

    REB_R r;

    // If actor is a HANDLE!, it should be a PAF
    //
    // !!! Review how user-defined types could make this better/safer, as if
    // it's some other kind of handle value this could crash.
    //
    if (Is_Native_Port_Actor(actor)) {
        r = cast(PORT_HOOK*, VAL_HANDLE_CFUNC(actor))(frame_, port, verb);
        goto post_process_output;
    }

    if (not IS_OBJECT(actor))
        fail (Error_Invalid_Actor_Raw());

    // Dispatch object function:

  blockscope {
    const bool strict = false;
    REBLEN n = Find_Symbol_In_Context(actor, VAL_WORD_SYMBOL(verb), strict);

    REBVAL *action = (n == 0) ? nullptr : CTX_VAR(VAL_CONTEXT(actor), n);
    if (not action or not IS_ACTION(action))
        fail (Error_No_Port_Action_Raw(verb));

    if (Redo_Action_Maybe_Stale_Throws(frame_->out, frame_, VAL_ACTION(action)))
        return R_THROWN;

    CLEAR_CELL_FLAG(frame_->out, OUT_NOTE_STALE);

    r = D_OUT; // result should be in frame_->out
  }

    // !!! READ's /LINES and /STRING refinements are something that should
    // work regardless of data source.  But R3-Alpha only implemented it in
    // %p-file.c, so it got ignored.  Ren-C caught that it was being ignored,
    // so the code was moved to here as a quick fix.
    //
    // !!! Note this code is incorrect for files read in chunks!!!

  post_process_output:

    if (VAL_WORD_ID(verb) == SYM_READ) {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));
        UNUSED(PAR(part));
        UNUSED(PAR(seek));

        if (not r)
            return nullptr;  // !!! `read dns://` returns nullptr on failure

        if (r != D_OUT) {
            if (Is_Api_Value(r)) {
                Handle_Api_Dispatcher_Result(frame_, r);
                r = D_OUT;
            }
            else
                assert(!"Bad REB_R in READ workaround for /STRING /LINES");
        }

        if ((REF(string) or REF(lines)) and not IS_TEXT(D_OUT)) {
            if (not IS_BINARY(D_OUT))
                fail ("/STRING or /LINES used on a non-BINARY!/STRING! read");

            REBSIZ size;
            const REBYTE *data = VAL_BINARY_SIZE_AT(&size, D_OUT);
            REBSTR *decoded = Make_Sized_String_UTF8(cs_cast(data), size);
            Init_Text(D_OUT, decoded);
        }

        if (REF(lines)) { // caller wants a BLOCK! of STRING!s, not one string
            assert(IS_TEXT(D_OUT));

            DECLARE_LOCAL (temp);
            Copy_Cell(temp, D_OUT);
            Init_Block(D_OUT, Split_Lines(temp));
        }
    }

    return r;
}


//
//  Make_Port_Actor_Handle: C
//
// When users write a "port scheme", they provide an actor...which contains
// a block of functions with the names of the "verbs" that can be applied to
// ports.  When the name of a port action matches the name of a supplied
// function, then the matching function is called.  Each of these functions
// may have different numbers and types of arguments and refinements.
//
// R3-Alpha provided some native code to handle port actions, but all the
// port actions were folded into a single function that was able to interpret
// different function frames.  This was similar to how datatypes handled
// various "action" verbs.
//
// In Ren-C, this distinction is taken care of such that when the actor is
// a HANDLE!, it is assumed to be a pointer to a "PORT_HOOK".  But since the
// registration is done in user code, these handles have to be exposed to
// that code.  In order to make this more distributed, each port action
// function is exposed through a native that returns it.  This is the shared
// routine used to make a handle out of a PORT_HOOK.
//
void Make_Port_Actor_Handle(REBVAL *out, PORT_HOOK paf)
{
    Init_Handle_Cfunc(out, cast(CFUNC*, paf));
}
