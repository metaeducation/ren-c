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
static REB_R Timer_Actor(REBFRM *frame_, REBCTX *port, REBCNT action)
{
    REBVAL *spec;
    REBVAL *state;
    REBCNT result;
    REBVAL *arg;

    DECLARE_LOCAL (save_port);

    arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    Move_Value(D_OUT, D_ARG(1));

    // Validate and fetch relevant PORT fields:
    state = CTX_VAR(port, STD_PORT_STATE);
    spec  = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec)) fail (Error_Invalid_Spec_Raw(spec));

    // Get or setup internal state data:
    if (!IS_BLOCK(state))
        Init_Block(state, Make_Array(127));

    switch (action) {

    case SYM_UPDATE:
        return R_BLANK;

    // Normal block actions done on events:
    case SYM_POKE:
        if (NOT(IS_EVENT(D_ARG(3))))
            fail (D_ARG(3));
        goto act_blk;
    case SYM_INSERT:
    case SYM_APPEND:
    //case SYM_PATH:      // not allowed: port/foo is port object field access
    //case SYM_PATH_SET:  // not allowed: above
        if (NOT(IS_EVENT(arg)))
            fail (arg);
    case SYM_PICK_P:
act_blk:
        Move_Value(&save_port, D_ARG(1)); // save for return
        Move_Value(D_ARG(1), state);
        result = T_Block(ds, action);
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
        RESET_ARRAY(state);
        CLR_FLAG(Eval_Signals, SIG_EVENT_PORT);
        break;

    case SYM_LENGTH:
        Init_Integer(D_OUT, VAL_LEN_HEAD(state));
        break;

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;
        if (!req) { //!!!
            req = OS_MAKE_DEVREQ(RDI_EVENT);
            SET_OPEN(req);
            OS_DO_DEVICE(req, RDC_CONNECT);     // stays queued
        }
        break; }

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

    return R_OUT;
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
REBNATIVE(get_timer_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Timer_Actor);
    return R_OUT;
}
