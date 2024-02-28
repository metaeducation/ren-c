//
//  File: %p-timer.c
//  Summary: "timer port interface"
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
// NOT IMPLEMENTED
//
/*
    General idea of usage:

    t: open timer://name
    write t 10  ; set timer - also allow: 1.23 1:23
    wait t
    clear t     ; reset or delete?
    read t      ; get timer value
    t/awake: func [event] [print "timer!"]
    one-shot vs restart timer
*/

#include "sys-core.h"


//
//  Timer_Actor: C
//
static REB_R Timer_Actor(REBFRM *frame_, Value* port, Value* verb)
{
    Value* arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBCTX *ctx = VAL_CONTEXT(port);
    Value* spec = CTX_VAR(ctx, STD_PORT_SPEC);
    if (!IS_OBJECT(spec))
        fail (Error_Invalid_Spec_Raw(spec));

    // Get or setup internal state data:
    //
    Value* state = CTX_VAR(ctx, STD_PORT_STATE);
    if (!IS_BLOCK(state))
        Init_Block(state, Make_Arr(127));

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        Option(SymId) property = Cell_Word_Id(ARG(property));
        assert(property != 0);

        switch (property) {
        case SYM_LENGTH:
            return Init_Integer(D_OUT, VAL_LEN_HEAD(state));

        default:
            break;
        }

        break; }

    case SYM_ON_WAKE_UP:
        return Init_Bar(D_OUT);

    // Normal block actions done on events:
    case SYM_POKE:
        if (not IS_EVENT(D_ARG(3)))
            fail (Error_Invalid(D_ARG(3)));
        goto act_blk;
    case SYM_INSERT:
    case SYM_APPEND:
    //case SYM_PATH:      // not allowed: port/foo is port object field access
    //case SYM_PATH_SET:  // not allowed: above
        if (not IS_EVENT(arg))
            fail (Error_Invalid(arg));
    case SYM_PICK: {
    act_blk:
        DECLARE_VALUE (save_port);
        Move_Value(&save_port, D_ARG(1)); // save for return
        Move_Value(D_ARG(1), state);

        const Value* r = T_Block(ds, verb);
        SET_SIGNAL(SIG_EVENT_PORT);
        if (
            verb == SYM_INSERT
            or verb == SYM_APPEND
            or verb == SYM_REMOVE
        ){
            Move_Value(D_OUT, save_port);
            return D_OUT;
        }
        return r; }

    case SYM_CLEAR:
        RESET_ARRAY(state);
        Eval_Signals &= ~SIG_EVENT_PORT;
        RETURN (port);

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;
        if (!req) { //!!!
            req = OS_MAKE_DEVREQ(RDI_EVENT);
            req->flags |= RRF_OPEN;

            OS_DO_DEVICE_SYNC(req, RDC_CONNECT);

            // "stays queued"
            // !!! or stays queued means it's pending?
        }
        RETURN (port); }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));
}


// !!! Timer code is currently not used
//x
//x  get-timer-actor-handle: native [
//x
//x  {Retrieve handle to the native actor for timer features}
//x
//x      return: [handle!]
//x  ]
//x
DECLARE_NATIVE(get_timer_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Timer_Actor);
    return D_OUT;
}
