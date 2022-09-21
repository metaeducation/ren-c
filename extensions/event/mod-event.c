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


Symbol(const*) S_Event(void) {
    return Canon(EVENT_X);
}


//
//  startup*: native [  ; Note: DO NOT EXPORT!
//
//  {Make the EVENT! datatype work with GENERIC actions, comparison ops, etc}
//
//      return: <none>
//  ]
//
DECLARE_NATIVE(startup_p)
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
    Builtin_Type_Hooks[k][IDX_SYMBOL_HOOK] = cast(CFUNC*, &S_Event);
    Builtin_Type_Hooks[k][IDX_GENERIC_HOOK] = cast(CFUNC*, &T_Event);
    Builtin_Type_Hooks[k][IDX_COMPARE_HOOK] = cast(CFUNC*, &CT_Event);
    Builtin_Type_Hooks[k][IDX_MAKE_HOOK] = cast(CFUNC*, &MAKE_Event);
    Builtin_Type_Hooks[k][IDX_TO_HOOK] = cast(CFUNC*, &TO_Event);
    Builtin_Type_Hooks[k][IDX_MOLD_HOOK] = cast(CFUNC*, &MF_Event);

    Startup_Events();  // initialize other event stuff

    return NONE;
}


//
//  shutdown*: native [  ; Note: DO NOT EXPORT!
//
//  {Remove behaviors for EVENT! added by REGISTER-EVENT-HOOKS}
//
//      return: <none>
//  ]
//
DECLARE_NATIVE(shutdown_p)
{
    EVENT_INCLUDE_PARAMS_OF_SHUTDOWN_P;

    // !!! See notes in register-event-hooks for why we reach below the
    // normal custom type machinery to pack an event into a single cell
    //
    const enum Reb_Kind k = REB_EVENT;
    Builtin_Type_Hooks[k][IDX_GENERIC_HOOK] = cast(CFUNC*, &T_Unhooked);
    Builtin_Type_Hooks[k][IDX_COMPARE_HOOK] = cast(CFUNC*, &CT_Unhooked);
    Builtin_Type_Hooks[k][IDX_MAKE_HOOK] = cast(CFUNC*, &MAKE_Unhooked);
    Builtin_Type_Hooks[k][IDX_TO_HOOK] = cast(CFUNC*, &TO_Unhooked);
    Builtin_Type_Hooks[k][IDX_MOLD_HOOK] = cast(CFUNC*, &MF_Unhooked);

    // !!! currently no shutdown code, but there once was for destroying an
    // invisible handle in windows...

    return NONE;
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
//  ]
//
DECLARE_NATIVE(wait_p)  // See wrapping function WAIT in usermode code
//
// WAIT* expects a BLOCK! argument to have been pre-reduced; this means it
// does not have to implement the reducing process "stacklessly" itself.  The
// stackless nature comes for free by virtue of REDUCE-ing in usermode.
{
    EVENT_INCLUDE_PARAMS_OF_WAIT_P;

    REBLEN timeout = 0;  // in milliseconds
    REBVAL *ports = nullptr;

    Cell(const*) val;
    if (not IS_BLOCK(ARG(value)))
        val = ARG(value);
    else {
        ports = ARG(value);

        REBLEN num_pending = 0;
        Cell(const*) tail;
        val = VAL_ARRAY_AT(&tail, ports);
        for (; val != tail; ++val) {  // find timeout
            if (IS_PORT(val))
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
            Array(*) single = Make_Array(1);
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

    while (wait_millisec != 0) {
        if (GET_SIGNAL(SIG_HALT)) {
            CLR_SIGNAL(SIG_HALT);

            return Init_Thrown_With_Label(FRAME, Lib(NULL), Lib(HALT));
        }

        if (GET_SIGNAL(SIG_INTERRUPT)) {
            CLR_SIGNAL(SIG_INTERRUPT);

            // !!! If implemented, this would allow triggering a breakpoint
            // with a keypress.  This needs to be thought out a bit more,
            // but may not involve much more than running `BREAKPOINT`.
            //
            fail ("BREAKPOINT from SIG_INTERRUPT not currently implemented");
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
        if (OS_Poll_Devices()) {
            //
            // Some activity, so use low wait time.
            //
            wait_millisec = 1;
            continue;
        }

        // No activity (nothing to do) so increase the wait time
        //
        wait_millisec *= 2;
        if (wait_millisec > MAX_WAIT_MS)
            wait_millisec = MAX_WAIT_MS;

        // Nothing, so wait for period of time

        unsigned int delta = Delta_Time(base_wait) / 1000 + res;
        if (delta >= wait_millisec)
            continue;

        wait_millisec -= delta; // account for time lost above

        Wait_Milliseconds_Interrupted(wait_millisec);
    }

    return nullptr;
}
