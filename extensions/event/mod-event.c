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

extern void Startup_Events(void);
extern void Shutdown_Events(void);

extern bool Wait_Milliseconds_Interrupted(unsigned int millisec);


//
//  startup*: native [  ; Note: DO NOT EXPORT!
//
//  {Make the EVENT! datatype work with GENERIC actions, comparison ops, etc}
//
//      return: <none>
//  ]
//
REBNATIVE(startup_p)
{
    EVENT_INCLUDE_PARAMS_OF_STARTUP_P;

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

    Startup_Events();  // initialize other event stuff

    return Init_None(D_OUT);
}


//
//  shutdown*: native [  ; Note: DO NOT EXPORT!
//
//  {Remove behaviors for EVENT! added by REGISTER-EVENT-HOOKS}
//
//      return: <none>
//  ]
//
REBNATIVE(shutdown_p)
{
    EVENT_INCLUDE_PARAMS_OF_SHUTDOWN_P;

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

    // !!! currently no shutdown code, but there once was for destroying an
    // invisible handle in windows...

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
            if (IS_PORT(val) and Is_Port_Pending(val))
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
            if (not Is_Port_Pending(val))
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
            fail (Error_Bad_Value(val));
        }
    }

    REBI64 base = Delta_Time(0);
    REBLEN wait_millisec = 1;
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
        Init_Word(Alloc_Tail_Array(a), Canon(ONLY));

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

    while (wait_millisec != 0) {
        if (GET_SIGNAL(SIG_HALT)) {
            CLR_SIGNAL(SIG_HALT);

            Init_Thrown_With_Label(D_OUT, Lib(NULL), Lib(HALT));
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
            wait_millisec *= 2;
            if (wait_millisec > MAX_WAIT_MS)
                wait_millisec = MAX_WAIT_MS;
        }
        else {
            // Call the system awake function.
            //
            // !!! Note: if we knew for certain the names of the arguments
            // we could use "APPLIQUE".  Since we don't, we have to use a
            // positional call...but a hybridized APPLY would help here.
            //
            if (rebRunThrows(
                D_OUT,
                true,  // fully
                REF(only) ? awake_only : awake,
                system_port,
                ports == nullptr ? Lib(BLANK) : ports
            )){
                fail (Error_No_Catch_For_Throw(D_OUT));
            }

            // Awake function returns true for end of WAIT
            //
            if (IS_LOGIC(D_OUT) and VAL_LOGIC(D_OUT)) {
                did_port_action = true;
                RESET(D_OUT);
                goto post_wait_loop;
            }

            // Some activity, so use low wait time.
            //
            wait_millisec = 1;

            RESET(D_OUT);
        }

        if (timeout != ALL_BITS) {
            //
            // Figure out how long that (and OS_WAIT) took:
            //
            REBLEN time = cast(REBLEN, Delta_Time(base) / 1000);
            if (time >= timeout)
                break;  // done (was dt = 0 before)
            else if (wait_millisec > timeout - time)  // use smaller residual time
                wait_millisec = timeout - time;
        }

        int64_t base_wait = Delta_Time(0);  // start timing

        // Let any pending device I/O have a chance to run:
        //
        if (OS_Poll_Devices())
            continue;

        // Nothing, so wait for period of time

        unsigned int delta = Delta_Time(base_wait) / 1000 + res;
        if (delta >= wait_millisec)
            continue;

        wait_millisec -= delta; // account for time lost above

        Wait_Milliseconds_Interrupted(wait_millisec);
    }

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
// !!! The only place WAKE-UP is called is by the system port's AWAKE function
// (usermode code).  The return result from WAKE-UP makes it decide whether to
// put a port into the "waked" queue, e.g. being a potential answer back from
// WAIT as a port that has something new to say, hence it should come out
// of the blocked state.
{
    EVENT_INCLUDE_PARAMS_OF_WAKE_UP;

    FAIL_IF_BAD_PORT(ARG(port));

    REBCTX *ctx = VAL_CONTEXT(ARG(port));

    REBVAL *actor = CTX_VAR(ctx, STD_PORT_ACTOR);
    if (Is_Native_Port_Actor(actor)) {
        /*
            DECLARE_LOCAL (verb);
            Init_Word(verb, Canon(ON_WAKE_UP));
            const REBVAL *r = Do_Port_Action(frame_, ARG(port), verb);
            assert(IS_BAD_WORD(r));
            UNUSED(r);
        */

        // !!! This gave native ports an opportunity to react to WAKE-UP if
        // they wanted to.  However, the native port is what sent the event
        // in the first place... so it's not really finding out anything it
        // didn't already know.  It just knows "oh, so you're sending that
        // event I raised now, are you?
        //
        // The real target of the event is any port layered on *top* of the
        // port, like an http layer sitting on top of the TCP layer.  They
        // registered an AWAKE function that gets called.
    }

    bool woke_up = true; // start by assuming success

    REBVAL *awake = CTX_VAR(ctx, STD_PORT_AWAKE);
    if (IS_ACTION(awake)) {
        const bool fully = true; // error if not all arguments consumed

        if (rebRunThrows(D_OUT, fully, awake, ARG(event)))
            fail (Error_No_Catch_For_Throw(D_OUT));

        if (not (IS_LOGIC(D_OUT) and VAL_LOGIC(D_OUT)))
            woke_up = false;

        RESET(D_OUT);
    }

    return Init_Logic(D_OUT, woke_up);
}
