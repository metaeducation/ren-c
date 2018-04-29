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
static REB_R Event_Actor(REBFRM *frame_, REBCTX *port, REBSYM verb)
{
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    // Validate and fetch relevant PORT fields:
    //
    REBVAL *state = CTX_VAR(port, STD_PORT_STATE);
    REBVAL *spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec))
        fail (Error_Invalid_Spec_Raw(spec));

    // Get or setup internal state data:
    //
    if (!IS_BLOCK(state))
        Init_Block(state, Make_Array(EVENTS_CHUNK - 1));

    switch (verb) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implicit in port
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            Init_Integer(D_OUT, VAL_LEN_HEAD(state));
            return R_OUT;

        default:
            break;
        }

        break; }

    case SYM_ON_WAKE_UP:
        return R_BLANK;

    // Normal block actions done on events:
    case SYM_POKE:
        if (!IS_EVENT(D_ARG(3)))
            fail (Error_Invalid(D_ARG(3)));
        goto act_blk;
    case SYM_INSERT:
    case SYM_APPEND:
        if (!IS_EVENT(arg))
            fail (Error_Invalid(arg));
        // falls through
    case SYM_PICK: {
    act_blk:;
        //
        // !!! For performance, this reuses the same frame built for the
        // INSERT/etc. on a PORT! to do an INSERT/etc. on whatever kind of
        // value the state is.  It saves the value of the port, substitutes
        // the state value in the first slot of the frame, and calls the
        // array type dispatcher.  :-/
        //
        DECLARE_LOCAL (save_port);
        Move_Value(save_port, D_ARG(1));
        Move_Value(D_ARG(1), state);

        REB_R r = T_Array(frame_, verb);
        SET_SIGNAL(SIG_EVENT_PORT);
        if (
            verb == SYM_INSERT
            || verb == SYM_APPEND
            || verb == SYM_REMOVE
        ){
            Move_Value(D_OUT, save_port);
            return R_OUT;
        }
        return r; }

    case SYM_CLEAR:
        TERM_ARRAY_LEN(VAL_ARRAY(state), 0);
        CLR_SIGNAL(SIG_EVENT_PORT);
        goto return_port;

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

        if (req == NULL) { //!!!
            req = OS_MAKE_DEVREQ(RDI_EVENT);
            req->flags |= RRF_OPEN;
            REBVAL *result = OS_DO_DEVICE(req, RDC_CONNECT);
            if (result == NULL) {
                //
                // comment said "stays queued", hence seems pending happens
            }
            else {
                if (rebDid("lib/error?", result, END))
                    rebFail (result, END);
                else {
                    assert(FALSE); // !!! can this happen?
                    rebRelease(result); // ignore result
                }
            }
        }
        goto return_port; }

    case SYM_CLOSE: {
        OS_ABORT_DEVICE(req);

        REBVAL *result = OS_DO_DEVICE(req, RDC_CLOSE);
        assert(result != NULL); // should be synchronous
        if (rebDid("lib/error?", result, END))
            rebFail (result, END);
        rebRelease(result);

        // free req!!!
        req->flags &= ~RRF_OPEN;
        req = NULL;
        goto return_port; }

    case SYM_FIND:
        break; // !!! R3-Alpha said "add it" (e.g. unimplemented)

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));

return_port:
    Move_Value(D_OUT, D_ARG(1));
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
