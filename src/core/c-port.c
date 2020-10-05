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
//  Force_Get_Port_State: C
//
// Use private state area in a port. Create if necessary.
// The size is that of a binary structure used by
// the port for storing internal information.
//
REBREQ *Force_Get_Port_State(const REBVAL *port, void *device)
{
    REBDEV *dev = cast(REBDEV*, device);
    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *state = CTX_VAR(ctx, STD_PORT_STATE);

    REBREQ *req;

    if (IS_BINARY(state)) {
        assert(VAL_INDEX(state) == 0);  // should always be at head
        assert(VAL_LEN_HEAD(state) == dev->req_size);  // should be right size
        req = VAL_BINARY_KNOWN_MUTABLE(state);
    }
    else {
        assert(IS_BLANK(state));
        req = OS_Make_Devreq(dev);
        ReqPortCtx(req) = ctx;  // Guarded: SERIES_INFO_MISC_NODE_NEEDS_MARK

        Init_Binary(state, SER(req));
    }

    return req;
}


//
//  Pending_Port: C
//
// Return true if port value is pending a signal.
// Not valid for all ports - requires request struct!!!
//
bool Pending_Port(const RELVAL *port)
{
    if (IS_PORT(port)) {
        REBVAL *state = CTX_VAR(VAL_CONTEXT(port), STD_PORT_STATE);

        if (IS_BINARY(state)) {
            REBREQ *req = VAL_BINARY_KNOWN_MUTABLE(state);
            if (not (Req(req)->flags & RRF_PENDING))
                return false;
        }
    }
    return true;
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

    REBLEN n; // goto would cross initialization
    n = Find_Canon_In_Context(
        VAL_CONTEXT(actor),
        VAL_WORD_CANON(verb),
        false // !always
    );

    REBVAL *action;
    if (n == 0 or not IS_ACTION(action = VAL_CONTEXT_VAR(actor, n)))
        fail (Error_No_Port_Action_Raw(verb));

    if (Redo_Action_Throws(frame_->out, frame_, VAL_ACTION(action)))
        return R_THROWN;

    r = D_OUT; // result should be in frame_->out

    // !!! READ's /LINES and /STRING refinements are something that should
    // work regardless of data source.  But R3-Alpha only implemented it in
    // %p-file.c, so it got ignored.  Ren-C caught that it was being ignored,
    // so the code was moved to here as a quick fix.
    //
    // !!! Note this code is incorrect for files read in chunks!!!

  post_process_output:

    if (VAL_WORD_SYM(verb) == SYM_READ) {
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

            REBSTR *decoded = Make_Sized_String_UTF8(
                cs_cast(VAL_BIN_AT(D_OUT)),
                VAL_LEN_AT(D_OUT)
            );
            Init_Text(D_OUT, decoded);
        }

        if (REF(lines)) { // caller wants a BLOCK! of STRING!s, not one string
            assert(IS_TEXT(D_OUT));

            DECLARE_LOCAL (temp);
            Move_Value(temp, D_OUT);
            Init_Block(D_OUT, Split_Lines(temp));
        }
    }

    return r;
}


//
//  Secure_Port: C
//
// kind: word that represents the type (e.g. 'file)
// req:  I/O request
// name: value that holds the original user spec
// path: the path to compare with
//
// !!! SECURE was not implemented in R3-Alpha.  This routine took a translated
// local path (as a REBSER) which had been expanded fully.  The concept of
// "local paths" is not something the core is going to be concerned with (e.g.
// backslash translation), rather something that the OS-specific extension
// code does.  If security is going to be implemented at a higher-level, then
// it may have to be in the PORT! code itself.  As it isn't active, it doesn't
// matter at the moment--but is a placeholder for finding the right place.
//
void Secure_Port(
    const REBSTR *kind,
    REBREQ *req,
    const REBVAL *name
    /* , const REBVAL *path */
){
    const REBVAL *path = name;
    assert(IS_FILE(path)); // !!! relative, untranslated
    UNUSED(path);

    if (Req(req)->modes & RFM_READ)
        Check_Security_Placeholder(STR_CANON(kind), SYM_READ, name);

    if (Req(req)->modes & RFM_WRITE)
        Check_Security_Placeholder(STR_CANON(kind), SYM_WRITE, name);
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
