//
//  File: %p-event.c
//  Summary: "event port interface"
//  Section: ports
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
/*
  Basics:

      Ports use requests to control devices.
      Devices do their best, and return when no more is possible.
      Progs call WAIT to check if devices have changed.
      If devices changed, modifies request, and sends event.
      If no devices changed, timeout happens.
      On REBOL side, we scan event queue.
      If we find an event, we call its port/awake function.

      Different cases exist:

      1. wait for time only

      2. wait for ports and time.  Need a master wait list to
         merge with the list provided this function.

      3. wait for windows to close - check each time we process
         a close event.

      4. what to do on console ESCAPE interrupt? Can use catch it?

      5. how dow we relate events back to their ports?

      6. async callbacks
*/

#include "sys-core.h"

REBREQ *req;        //!!! move this global

#define EVENTS_LIMIT 0xFFFF //64k
#define EVENTS_CHUNK 128

//
//  Append_Event: C
//
// Append an event to the end of the current event port queue.
// Return a pointer to the event value.
//
// Note: this function may be called from out of environment,
// so do NOT extend the event queue here. If it does not have
// space, return 0. (Should it overwrite or wrap???)
//
REBVAL *Append_Event(void)
{
    REBVAL *port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (!IS_PORT(port)) return 0; // verify it is a port object

    // Get queue block:
    REBVAL *state = VAL_CONTEXT_VAR(port, STD_PORT_STATE);
    if (!IS_BLOCK(state)) return 0;

    // Append to tail if room:
    if (SER_FULL(VAL_SERIES(state))) {
        if (VAL_LEN_HEAD(state) > EVENTS_LIMIT)
            panic (state);

        Extend_Series(VAL_SERIES(state), EVENTS_CHUNK);
    }
    TERM_ARRAY_LEN(VAL_ARRAY(state), VAL_LEN_HEAD(state) + 1);

    REBVAL *value = SINK(ARR_LAST(VAL_ARRAY(state)));
    Init_Blank(value);

    return value;
}


//
//  Find_Last_Event: C
//
// Find the last event in the queue by the model
// Check its type, if it matches, then return the event or NULL
//
REBVAL *Find_Last_Event(REBINT model, REBINT type)
{
    REBVAL *port;
    RELVAL *value;
    REBVAL *state;

    port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (!IS_PORT(port)) return NULL; // verify it is a port object

    // Get queue block:
    state = VAL_CONTEXT_VAR(port, STD_PORT_STATE);
    if (!IS_BLOCK(state)) return NULL;

    value = VAL_ARRAY_TAIL(state) - 1;
    for (; value >= VAL_ARRAY_HEAD(state); --value) {
        if (VAL_EVENT_MODEL(value) == model) {
            if (VAL_EVENT_TYPE(value) == type) {
                return KNOWN(value);
            } else {
                return NULL;
            }
        }
    }

    return NULL;
}

//
//  Event_Actor: C
//
// Internal port handler for events.
//
static REB_R Event_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    REBVAL *spec;
    REBVAL *state;
    REB_R result;
    REBVAL *arg;

    DECLARE_LOCAL (save_port);

    arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    Move_Value(D_OUT, D_ARG(1));

    // Validate and fetch relevant PORT fields:
    state = CTX_VAR(port, STD_PORT_STATE);
    spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec)) fail (Error_Invalid_Spec_Raw(spec));

    // Get or setup internal state data:
    if (!IS_BLOCK(state))
        Init_Block(state, Make_Array(EVENTS_CHUNK - 1));

    switch (action) {

    case SYM_UPDATE:
        return R_BLANK;

    // Normal block actions done on events:
    case SYM_POKE:
        if (!IS_EVENT(D_ARG(3)))
            fail (D_ARG(3));
        goto act_blk;
    case SYM_INSERT:
    case SYM_APPEND:
    //case A_PATH:      // not allowed: port/foo is port object field access
    //case A_PATH_SET:  // not allowed: above
        if (!IS_EVENT(arg))
            fail (arg);
    case SYM_PICK_P:
act_blk:
        Move_Value(save_port, D_ARG(1)); // save for return
        Move_Value(D_ARG(1), state);
        result = T_Array(frame_, action);
        SET_SIGNAL(SIG_EVENT_PORT);
        if (
            action == SYM_INSERT
            || action == SYM_APPEND
            || action == SYM_REMOVE
        ){
            Move_Value(D_OUT, save_port);
            break;
        }
        return result; // return condition

    case SYM_CLEAR:
        TERM_ARRAY_LEN(VAL_ARRAY(state), 0);
        CLR_SIGNAL(SIG_EVENT_PORT);
        break;

    case SYM_LENGTH:
        Init_Integer(D_OUT, VAL_LEN_HEAD(state));
        break;

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));
        if (REF(new))
            fail (Error_Bad_Refines_Raw());
        if (REF(read))
            fail (Error_Bad_Refines_Raw());
        if (REF(write))
            fail (Error_Bad_Refines_Raw());
        if (REF(seek))
            fail (Error_Bad_Refines_Raw());
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }

        if (!req) { //!!!
            req = OS_MAKE_DEVREQ(RDI_EVENT);
            if (req) {
                SET_OPEN(req);
                OS_DO_DEVICE(req, RDC_CONNECT);     // stays queued
            }
        }
        break; }

    case SYM_CLOSE:
        OS_ABORT_DEVICE(req);
        OS_DO_DEVICE(req, RDC_CLOSE);
        // free req!!!
        SET_CLOSED(req);
        req = 0;
        break;

    case SYM_FIND: // add it

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

    return R_OUT;
}


//
//  Startup_Event_Scheme: C
//
void Startup_Event_Scheme(void)
{
    req = 0; // move to port struct
}


//
//  Shutdown_Event_Scheme: C
//
void Shutdown_Event_Scheme(void)
{
    if (req) {
        OS_FREE(req);
        req = NULL;
    }
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
    return R_OUT;
}
