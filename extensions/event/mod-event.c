//
//  File: %mod-event.c
//  Summary: "EVENT! extension main C file"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologiesg
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// See notes in %extensions/event/README.md
//

#include "sys-core.h"

#include "tmp-mod-event.h"

#include "reb-event.h"

//
//  register-event-hooks: native [
//
//  {Make the EVENT! datatype work with GENERIC actions, comparison ops, etc}
//
//      return: <none>
//  ]
//
REBNATIVE(register_event_hooks)
{
    EVENT_INCLUDE_PARAMS_OF_REGISTER_EVENT_HOOKS;

    OS_Register_Device(&Dev_Event);

    // !!! See notes on Hook_Datatype for this poor-man's substitute for a
    // coherent design of an extensible object system (as per Lisp's CLOS)
    //
    // !!! EVENT has a specific desire to use *all* of the bits in the cell.
    // However, extension types generally do not have this option.  So we
    // make a special exemption and allow REB_EVENT to take one of the
    // builtin type bytes, so it can use the EXTRA() for more data.  This
    // may or may not be worth it for this case...but it's a demonstration of
    // a degree of freedom that we have.

    const enum Reb_Kind k = REB_EVENT;
    Builtin_Type_Hooks[k][IDX_GENERIC_HOOK] = cast(CFUNC*, &T_Event);
    Builtin_Type_Hooks[k][IDX_PATH_HOOK] = cast(CFUNC*, &PD_Event);
    Builtin_Type_Hooks[k][IDX_COMPARE_HOOK] = cast(CFUNC*, &CT_Event);
    Builtin_Type_Hooks[k][IDX_MAKE_HOOK] = cast(CFUNC*, &MAKE_Event);
    Builtin_Type_Hooks[k][IDX_TO_HOOK] = cast(CFUNC*, &TO_Event);
    Builtin_Type_Hooks[k][IDX_MOLD_HOOK] = cast(CFUNC*, &MF_Event);

    Startup_Event_Scheme();

    return Init_None(D_OUT);
}


//
//  unregister-event-hooks: native [
//
//  {Remove behaviors for EVENT! added by REGISTER-EVENT-HOOKS}
//
//      return: <none>
//  ]
//
REBNATIVE(unregister_event_hooks)
{
    EVENT_INCLUDE_PARAMS_OF_UNREGISTER_EVENT_HOOKS;

    Shutdown_Event_Scheme();

    // !!! See notes in register-event-hooks for why we reach below the
    // normal custom type machinery to pack an event into a single cell
    //
    const enum Reb_Kind k = REB_EVENT;
    Builtin_Type_Hooks[k][IDX_GENERIC_HOOK] = cast(CFUNC*, &T_Unhooked);
    Builtin_Type_Hooks[k][IDX_PATH_HOOK] = cast(CFUNC*, &PD_Unhooked);
    Builtin_Type_Hooks[k][IDX_COMPARE_HOOK] = cast(CFUNC*, &CT_Unhooked);
    Builtin_Type_Hooks[k][IDX_MAKE_HOOK] = cast(CFUNC*, &MAKE_Unhooked);
    Builtin_Type_Hooks[k][IDX_TO_HOOK] = cast(CFUNC*, &TO_Unhooked);
    Builtin_Type_Hooks[k][IDX_MOLD_HOOK] = cast(CFUNC*, &MF_Unhooked);

    return Init_None(D_OUT);
}


//
//  get-event-actor-handle: native [
//
//  {Retrieve handle to the native actor for events (system, event, callback)}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_event_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Event_Actor);
    return D_OUT;
}


//
//  map-event: native [
//
//  {Returns event with inner-most graphical object and coordinate.}
//
//      return: [event!]
//      event [event!]
//  ]
//
REBNATIVE(map_event)
{
    EVENT_INCLUDE_PARAMS_OF_MAP_EVENT;

    REBVAL *e = ARG(event);

    if (VAL_EVENT_MODEL(e) != EVM_GUI)
        fail ("Can't use MAP-EVENT on non-GUI event");

    REBGOB *g = cast(REBGOB*, VAL_EVENT_NODE(e));
    if (not g)
        RETURN (e);  // !!! Should this have been an error?

    if (not (VAL_EVENT_FLAGS(e) & EVF_HAS_XY))
        RETURN (e);  // !!! Should this have been an error?

    REBD32 x = VAL_EVENT_X(e);
    REBD32 y = VAL_EVENT_Y(e);

    DECLARE_LOCAL (gob);
    Init_Gob(gob, g);  // !!! Efficiency hack: %reb-event.h has Init_Gob()
    PUSH_GC_GUARD(gob);

    REBVAL *mapped = rebValue(
        "map-gob-offset", gob, "make pair! [", rebI(x), rebI(y), "]"
    );

    // For efficiency, %reb-event.h is able to store direct REBGOB pointers
    // (This loses any index information or other cell-instance properties)
    //
    assert(VAL_EVENT_MODEL(e) == EVM_GUI);  // should still be true
    SET_VAL_EVENT_NODE(e, VAL_GOB(mapped));

    rebRelease(mapped);

    assert(VAL_EVENT_FLAGS(e) & EVF_HAS_XY);  // should still be true
    SET_VAL_EVENT_X(e, ROUND_TO_INT(x));
    SET_VAL_EVENT_Y(e, ROUND_TO_INT(y));

    RETURN (e);
}


//
//  Wait_For_Device_Events_Interruptible: C
//
// Check if devices need attention, and if not, then wait.
// The wait can be interrupted by a GUI event, otherwise
// the timeout will wake it.
//
// Res specifies resolution. (No wait if less than this.)
//
// Returns:
//     -1: Devices have changed state.
//      0: past given millsecs
//      1: wait in timer
//
// The time it takes for the devices to be scanned is
// subtracted from the timer value.
//
int Wait_For_Device_Events_Interruptible(
    unsigned int millisec,
    unsigned int res
){
    // printf("Wait_For_Device_Events_Interruptible %d\n", millisec);

    int64_t base = Delta_Time(0); // start timing

    // !!! The request is created here due to a comment that said "setup for
    // timing" and said it was okay to stack allocate it because "QUERY
    // below does not store it".  Having eliminated stack-allocated REBREQ,
    // it's not clear if it makes sense to allocate it here vs. below.
    //
    REBREQ *req = OS_Make_Devreq(&Dev_Event);

    // !!! This was an API addded to the HostKit at some point.  It was only
    // called here in event processing, so it's moved to the event extension.
    //
    Reap_Process(-1, NULL, 0);

    // Let any pending device I/O have a chance to run:
    //
    if (OS_Poll_Devices()) {
        Free_Req(req);
        return -1;
    }

    // Nothing, so wait for period of time

    unsigned int delta = Delta_Time(base) / 1000 + res;
    if (delta >= millisec) {
        Free_Req(req);
        return 0;
    }

    millisec -= delta; // account for time lost above
    Req(req)->length = millisec;

    // printf("Wait: %d ms\n", millisec);

    // Comment said "wait for timer or other event"
    //
    OS_DO_DEVICE_SYNC(req, RDC_QUERY);

    Free_Req(req);

    return 1;  // layer above should check delta again
}


#define MAX_WAIT_MS 64 // Maximum millsec to sleep


//
//  export wait*: native [
//
//  "Waits for a duration, port, or both."
//
//      return: "NULL if timeout, PORT! that awoke or BLOCK! of ports if /ALL"
//          [<opt> port! block!]
//      value [<opt> any-number! time! port! block!]
//      /all "Returns all in a block"
//      /only "only check for ports given in the block to this function"
//  ]
//
REBNATIVE(wait_p)  // See wrapping function WAIT in usermode code
//
// WAIT* expects a BLOCK! argument to have been pre-reduced; this means it
// does not have to implement the reducing process "stacklessly" itself.  The
// stackless nature comes for free by virtue of REDUCE-ing in usermode.
{
    EVENT_INCLUDE_PARAMS_OF_WAIT_P;

    REBLEN timeout = 0;  // in milliseconds
    REBVAL *ports = nullptr;

    const RELVAL *val;
    if (not IS_BLOCK(ARG(value)))
        val = ARG(value);
    else {
        ports = ARG(value);

        REBLEN num_pending = 0;
        const RELVAL *tail;
        val = VAL_ARRAY_AT(&tail, ports);
        for (; val != tail; ++val) {  // find timeout
            if (Pending_Port(val))
                ++num_pending;

            if (IS_INTEGER(val) or IS_DECIMAL(val) or IS_TIME(val))
                break;
        }
        if (val == tail) {
            if (num_pending == 0)
                return nullptr; // has no pending ports!
            timeout = ALL_BITS; // no timeout provided
            val = nullptr;
        }
    }

    if (val != nullptr) {
        switch (VAL_TYPE(val)) {
          case REB_INTEGER:
          case REB_DECIMAL:
          case REB_TIME:
            timeout = Milliseconds_From_Value(val);
            break;

          case REB_PORT: {
            if (not Pending_Port(val))
                return nullptr;

            REBARR *single = Make_Array(1);
            Append_Value(single, SPECIFIC(val));
            Init_Block(ARG(value), single);
            ports = ARG(value);

            timeout = ALL_BITS;
            break; }

          case REB_BLANK:
            timeout = ALL_BITS; // wait for all windows
            break;

          default:
            fail (Error_Bad_Value_Core(val, SPECIFIED));
        }
    }

    REBI64 base = Delta_Time(0);
    REBLEN wait_time = 1;
    REBLEN res = (timeout >= 1000) ? 0 : 16;  // OS dependent?

    // Waiting opens the doors to pressing Ctrl-C, which may get this code
    // to throw an error.  There needs to be a state to catch it.
    //
    assert(TG_Jump_List != nullptr);

    REBVAL *system_port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (not IS_PORT(system_port))
        fail ("System Port is not a PORT! object");

    REBCTX *sys = VAL_CONTEXT(system_port);

    REBVAL *waiters = CTX_VAR(sys, STD_PORT_STATE);
    if (not IS_BLOCK(waiters))
        fail ("Wait queue block in System Port is not a BLOCK!");

    REBVAL *waked = CTX_VAR(sys, STD_PORT_DATA);
    if (not IS_BLOCK(waked))
        fail ("Waked queue block in System Port is not a BLOCK!");

    REBVAL *awake = CTX_VAR(sys, STD_PORT_AWAKE);
    if (not IS_ACTION(awake))
        fail ("System Port AWAKE field is not an ACTION!");

    REBVAL *awake_only = D_SPARE;
    if (REF(only)) {
        //
        // If we're using /ONLY, we need path AWAKE/ONLY to call.  (The
        // va_list API does not support positional-provided refinements.)
        //
        REBARR *a = Make_Array(2);
        Append_Value(a, awake);
        Init_Word(Alloc_Tail_Array(a), Canon(SYM_ONLY));

        REBVAL *p = Try_Init_Path_Arraylike(D_SPARE, a);
        assert(p);  // `awake/only` doesn't contain any non-path-elements
        UNUSED(p);
    }
    else {
      #if !defined(NDEBUG)
        Init_Trash(D_SPARE);
      #endif
    }

    bool did_port_action = false;

    while (wait_time != 0) {
        if (GET_SIGNAL(SIG_HALT)) {
            CLR_SIGNAL(SIG_HALT);

            Init_Thrown_With_Label(D_OUT, NULLED_CELL, Native(HALT));
            return R_THROWN;
        }

        if (GET_SIGNAL(SIG_INTERRUPT)) {
            CLR_SIGNAL(SIG_INTERRUPT);

            // !!! If implemented, this would allow triggering a breakpoint
            // with a keypress.  This needs to be thought out a bit more,
            // but may not involve much more than running `BREAKPOINT`.
            //
            fail ("BREAKPOINT from SIG_INTERRUPT not currently implemented");
        }

        if (VAL_LEN_HEAD(waiters) == 0 and VAL_LEN_HEAD(waked) == 0) {
            //
            // No activity (nothing to do) so increase the wait time
            //
            wait_time *= 2;
            if (wait_time > MAX_WAIT_MS)
                wait_time = MAX_WAIT_MS;
        }
        else {
            // Call the system awake function.
            //
            // !!! Note: if we knew for certain the names of the arguments
            // we could use "APPLIQUE".  Since we don't, we have to use a
            // positional call...but a hybridized APPLY would help here.
            //
            if (RunQ_Throws(
                D_OUT,
                true,  // fully
                rebU(REF(only) ? awake_only : awake),
                system_port,
                ports == nullptr ? BLANK_VALUE : ports,
                rebEND
            )) {
                fail (Error_No_Catch_For_Throw(D_OUT));
            }

            // Awake function returns true for end of WAIT
            //
            if (IS_LOGIC(D_OUT) and VAL_LOGIC(D_OUT)) {
                did_port_action = true;
                goto post_wait_loop;
            }

            // Some activity, so use low wait time.
            //
            wait_time = 1;
        }

        if (timeout != ALL_BITS) {
            //
            // Figure out how long that (and OS_WAIT) took:
            //
            REBLEN time = cast(REBLEN, Delta_Time(base) / 1000);
            if (time >= timeout)
                break;  // done (was dt = 0 before)
            else if (wait_time > timeout - time)  // use smaller residual time
                wait_time = timeout - time;
        }

        //printf("%d %d %d\n", dt, time, timeout);

        Wait_For_Device_Events_Interruptible(wait_time, res);
    }

    //time = (REBLEN)Delta_Time(base);
    //Print("dt: %d", time);

  post_wait_loop:

    if (not did_port_action) {  // timeout
        SET_SERIES_LEN(VAL_ARRAY_KNOWN_MUTABLE(waked), 0);  // !!! Reset_Array?
        return nullptr;
    }

    if (not ports)
        return nullptr;

    // Determine what port(s) waked us (intersection of waked and ports)
    //
    // !!! Review: should intersect be mutating, or at least have a variant
    // like INTERSECT and INTERSECTED?  The original "Sieve_Ports" in R3-Alpha
    // had custom code here but this just uses the API.

    REBVAL *sieved = rebValue("intersect", ports, waked);
    Copy_Cell(D_OUT, sieved);
    rebRelease(sieved);

    SET_SERIES_LEN(VAL_ARRAY_KNOWN_MUTABLE(waked), 0);  // !!! Reset_Array?

    if (REF(all))
        return D_OUT;  // caller wants all the ports that waked us

    const RELVAL *first = VAL_ARRAY_ITEM_AT(D_OUT);
    if (not IS_PORT(first)) {
        assert(!"First element of intersection not port, does this happen?");
        return nullptr;
    }

    RETURN (SPECIFIC(first));
}


//
//  export wake-up: native [
//
//  "Awake and update a port with event."
//
//      return: [logic!]
//      port [port!]
//      event [event!]
//  ]
//
REBNATIVE(wake_up)
//
// Calls port update for native actors.
// Calls port awake function.
{
    EVENT_INCLUDE_PARAMS_OF_WAKE_UP;

    FAIL_IF_BAD_PORT(ARG(port));

    REBCTX *ctx = VAL_CONTEXT(ARG(port));

    REBVAL *actor = CTX_VAR(ctx, STD_PORT_ACTOR);
    if (Is_Native_Port_Actor(actor)) {
        //
        // We don't pass `actor` or `event` in, because we just pass the
        // current call info.  The port action can re-read the arguments.
        //
        // !!! Most of the R3-Alpha event model is around just as "life
        // support".  Added assertion and convention here that this call
        // doesn't throw or return meaningful data... (?)
        //
        DECLARE_LOCAL (verb);
        Init_Word(verb, Canon(SYM_ON_WAKE_UP));
        const REBVAL *r = Do_Port_Action(frame_, ARG(port), verb);
        assert(IS_BAD_WORD(r));
        UNUSED(r);
    }

    bool woke_up = true; // start by assuming success

    REBVAL *awake = CTX_VAR(ctx, STD_PORT_AWAKE);
    if (IS_ACTION(awake)) {
        const bool fully = true; // error if not all arguments consumed

        if (RunQ_Throws(D_OUT, fully, rebU(awake), ARG(event), rebEND))
            fail (Error_No_Catch_For_Throw(D_OUT));

        if (not (IS_LOGIC(D_OUT) and VAL_LOGIC(D_OUT)))
            woke_up = false;
    }

    return Init_Logic(D_OUT, woke_up);
}
