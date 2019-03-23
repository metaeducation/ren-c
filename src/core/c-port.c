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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See comments in Init_Ports for startup.
// See www.rebol.net/wiki/Event_System for full details.
//

#include "sys-core.h"

#define MAX_WAIT_MS 64 // Maximum millsec to sleep


//
//  Ensure_Port_State: C
//
// Use private state area in a port. Create if necessary.
// The size is that of a binary structure used by
// the port for storing internal information.
//
REBREQ *Ensure_Port_State(REBVAL *port, REBCNT device)
{
    assert(device < RDI_MAX);

    REBDEV *dev = Devices[device];
    if (not dev)
        return nullptr;

    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *state = CTX_VAR(ctx, STD_PORT_STATE);

    REBREQ *req;

    if (IS_BINARY(state)) {
        assert(VAL_INDEX(state) == 0);  // should always be at head
        assert(VAL_LEN_HEAD(state) == dev->req_size);  // should be right size
        req = VAL_BINARY(state);
    }
    else {
        assert(IS_BLANK(state));
        req = OS_MAKE_DEVREQ(device);
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
bool Pending_Port(REBVAL *port)
{
    if (IS_PORT(port)) {
        REBVAL *state = CTX_VAR(VAL_CONTEXT(port), STD_PORT_STATE);

        if (IS_BINARY(state)) {
            REBREQ *req = VAL_BINARY(state);
            if (not (Req(req)->flags & RRF_PENDING))
                return false;
        }
    }
    return true;
}


//
//  Awake_System: C
//
// Returns:
//     -1 for errors
//      0 for nothing to do
//      1 for wait is satisifed
//
REBINT Awake_System(REBARR *ports, bool only)
{
    // Get the system port object:
    REBVAL *port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (!IS_PORT(port))
        return -10; // verify it is a port object

    // Get wait queue block (the state field):
    REBVAL *state = VAL_CONTEXT_VAR(port, STD_PORT_STATE);
    if (!IS_BLOCK(state))
        return -10;

    // Get waked queue block:
    REBVAL *waked = VAL_CONTEXT_VAR(port, STD_PORT_DATA);
    if (!IS_BLOCK(waked))
        return -10;

    // If there is nothing new to do, return now:
    if (VAL_LEN_HEAD(state) == 0 and VAL_LEN_HEAD(waked) == 0)
        return -1;

    // Get the system port AWAKE function:
    REBVAL *awake = VAL_CONTEXT_VAR(port, STD_PORT_AWAKE);
    if (not IS_ACTION(awake))
        return -1;

    DECLARE_LOCAL (tmp);
    if (ports)
        Init_Block(tmp, ports);
    else
        Init_Blank(tmp);

    DECLARE_LOCAL (awake_only);
    if (only) {
        //
        // If we're using /ONLY, we need path AWAKE/ONLY to call.  (Ren-C's
        // va_list API does not support positionally-provided refinements.)
        //
        REBARR *a = Make_Array(2);
        Append_Value(a, awake);
        Init_Word(Alloc_Tail_Array(a), Canon(SYM_ONLY));

        Init_Path(awake_only, a);
    }

    // Call the system awake function:
    //
    DECLARE_LOCAL (result);
    if (RunQ_Throws(
        result,
        true, // fully
        rebU1(only ? awake_only : awake),
        port,
        tmp,
        rebEND
    )) {
        fail (Error_No_Catch_For_Throw(result));
    }

    // Awake function returns 1 for end of WAIT:
    //
    return (IS_LOGIC(result) and VAL_LOGIC(result)) ? 1 : 0;
}


//
//  Wait_Ports_Throws: C
//
// Inputs:
//     Ports: a block of ports or zero (on stack to avoid GC).
//     Timeout: milliseconds to wait
//
// Returns:
//     out is LOGIC! TRUE when port action happened, or FALSE for timeout
//     if a throw happens, out will be the thrown value and returns TRUE
//
bool Wait_Ports_Throws(
    REBVAL *out,
    REBARR *ports,
    REBCNT timeout,
    bool only
){
    REBI64 base = OS_DELTA_TIME(0);
    REBCNT time;
    REBCNT wt = 1;
    REBCNT res = (timeout >= 1000) ? 0 : 16;  // OS dependent?

    // Waiting opens the doors to pressing Ctrl-C, which may get this code
    // to throw an error.  There needs to be a state to catch it.
    //
    assert(Saved_State != NULL);

    while (wt) {
        if (GET_SIGNAL(SIG_HALT)) {
            CLR_SIGNAL(SIG_HALT);

            Init_Thrown_With_Label(out, NULLED_CELL, NAT_VALUE(halt));
            return true; // thrown
        }

        if (GET_SIGNAL(SIG_INTERRUPT)) {
            CLR_SIGNAL(SIG_INTERRUPT);

            // !!! If implemented, this would allow triggering a breakpoint
            // with a keypress.  This needs to be thought out a bit more,
            // but may not involve much more than running `BREAKPOINT`.
            //
            fail ("BREAKPOINT from SIG_INTERRUPT not currently implemented");
        }

        REBINT ret;

        // Process any waiting events:
        if ((ret = Awake_System(ports, only)) > 0) {
            Move_Value(out, TRUE_VALUE); // port action happened
            return false; // not thrown
        }

        // If activity, use low wait time, otherwise increase it:
        if (ret == 0) wt = 1;
        else {
            wt *= 2;
            if (wt > MAX_WAIT_MS) wt = MAX_WAIT_MS;
        }
        REBVAL *pump = Get_System(SYS_PORTS, PORTS_PUMP);
        if (not IS_BLOCK(pump))
            fail ("system/ports/pump must be a block");

        DECLARE_LOCAL (result);
        if (Do_Any_Array_At_Throws(result, pump, SPECIFIED))
            fail (Error_No_Catch_For_Throw(result));

        if (timeout != ALL_BITS) {
            // Figure out how long that (and OS_WAIT) took:
            time = cast(REBCNT, OS_DELTA_TIME(base) / 1000);
            if (time >= timeout) break;   // done (was dt = 0 before)
            else if (wt > timeout - time) // use smaller residual time
                wt = timeout - time;
        }

        //printf("%d %d %d\n", dt, time, timeout);

        // Wait for events or time to expire:
        OS_WAIT(wt, res);
    }

    //time = (REBCNT)OS_DELTA_TIME(base);
    //Print("dt: %d", time);

    Move_Value(out, FALSE_VALUE); // timeout;
    return false; // not thrown
}


//
//  Sieve_Ports: C
//
// Remove all ports not found in the WAKE list.
// ports could be NULL, in which case the WAKE list is cleared.
//
void Sieve_Ports(REBARR *ports)
{
    REBVAL *port;
    REBVAL *waked;
    REBCNT n;

    port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (!IS_PORT(port)) return;
    waked = VAL_CONTEXT_VAR(port, STD_PORT_DATA);
    if (!IS_BLOCK(waked)) return;

    for (n = 0; ports and n < ARR_LEN(ports);) {
        RELVAL *val = ARR_AT(ports, n);
        if (IS_PORT(val)) {
            assert(VAL_LEN_HEAD(waked) != 0);
            if (
                Find_In_Array_Simple(VAL_ARRAY(waked), 0, val)
                == VAL_LEN_HEAD(waked) // `=len` means not found
            ) {
                Remove_Series_Len(SER(ports), n, 1);
                continue;
            }
        }
        n++;
    }
    //clear waked list
    RESET_ARRAY(VAL_ARRAY(waked));
}


//
//  Redo_Action_Throws: C
//
// This code takes a running call frame that has been built for one action
// and then tries to map its parameters to invoke another action.  The new
// action may have different orders and names of parameters.
//
// R3-Alpha had a rather brittle implementation, that had no error checking
// and repetition of logic in Eval_Core.  Ren-C more simply builds a PATH! of
// the target function and refinements, passing args with EVAL_FLAG_EVAL_ONLY.
//
// !!! This could be done more efficiently now by pushing the refinements to
// the stack and using an APPLY-like technique.
//
// !!! This still isn't perfect and needs reworking, as it won't stand up in
// the face of targets that are "adversarial" to the archetype:
//
//     foo: func [a /b c] [...]  =>  bar: func [/b d e] [...]
//                    foo/b 1 2  =>  bar/b 1 2
//
bool Redo_Action_Throws(REBVAL *out, REBFRM *f, REBACT *run)
{
    REBARR *code_arr = Make_Array(FRM_NUM_ARGS(f)); // max, e.g. no refines
    RELVAL *code = ARR_HEAD(code_arr);

    // !!! For the moment, if refinements are needed we generate a PATH! with
    // the ACTION! at the head, and have the evaluator rediscover the stack
    // of refinements.  This would be better if we left them on the stack
    // and called into the evaluator with Begin_Action() already in progress
    // on a new frame.  Improve when time permits.
    //
    REBDSP dsp_orig = DSP; // we push refinements as we find them
    Move_Value(DS_PUSH(), ACT_ARCHETYPE(run)); // !!! Review: binding?

    assert(IS_END(f->param)); // okay to reuse, if it gets put back...
    f->param = ACT_PARAMS_HEAD(FRM_PHASE(f));
    f->arg = FRM_ARGS_HEAD(f);
    f->special = ACT_SPECIALTY_HEAD(FRM_PHASE(f));

    for (; NOT_END(f->param); ++f->param, ++f->arg, ++f->special) {
        if (Is_Param_Hidden(f->param)) {  // specialized-out parameter
            assert(GET_CELL_FLAG(f->special, ARG_MARKED_CHECKED));
            continue;
        }

        Reb_Param_Class pclass = VAL_PARAM_CLASS(f->param);

        if (
            pclass == REB_P_LOCAL
            or pclass == REB_P_RETURN
        ){
             continue; // don't add a callsite expression for it (can't)!
        }

        if (TYPE_CHECK(f->param, REB_TS_REFINEMENT)) {
            if (IS_BLANK(f->arg))  // don't add to PATH!
                continue;

            Init_Word(DS_PUSH(), VAL_PARAM_SPELLING(f->param));

            if (Is_Typeset_Invisible(f->param)) {
                assert(IS_REFINEMENT(f->arg));  // used but argless refinement
                continue;
            }
        }

        // The arguments were already evaluated to put them in the frame, do
        // not evaluate them again.
        //
        // !!! This tampers with the VALUE_FLAG_UNEVALUATED bit, which is
        // another good reason this should probably be done another way.  It
        // also loses information about the const bit.
        //
        Quotify(Move_Value(code, f->arg), 1);
        ++code;
    }

    TERM_ARRAY_LEN(code_arr, code - ARR_HEAD(code_arr));
    Manage_Array(code_arr);

    DECLARE_LOCAL (first);
    if (DSP == dsp_orig + 1) { // no refinements, just use ACTION!
        DS_DROP_TO(dsp_orig);
        Move_Value(first, ACT_ARCHETYPE(run));
    }
    else
        Init_Path(first, Pop_Stack_Values(dsp_orig));

    bool threw = Do_At_Mutable_Maybe_Stale_Throws(
        out,  // invisibles allow for out to not be Init_Void()'d
        first, // path not in array, will be "virtual" first element
        code_arr,
        0, // index
        SPECIFIED // reusing existing REBVAL arguments, no relative values
    );
    CLEAR_CELL_FLAG(out, OUT_MARKED_STALE);
    return threw;
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

    REBCNT n; // goto would cross initialization
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
    REBSTR *kind,
    REBREQ *req,
    const REBVAL *name
    /* , const REBVAL *path */
){
    const REBVAL *path = name;
    assert(IS_FILE(path)); // !!! relative, untranslated

    const REBYTE *flags = Security_Policy(STR_CANON(kind), path);

    // Check policy integer:
    // Mask is [xxxx wwww rrrr] - each holds the action
    if (Req(req)->modes & RFM_READ)
        Trap_Security(flags[POL_READ], STR_CANON(kind), name);

    if (Req(req)->modes & RFM_WRITE)
        Trap_Security(flags[POL_WRITE], STR_CANON(kind), name);
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
