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
// Copyright 2012-2016 Rebol Open Source Contributors
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
//  Event_Actor: C
//
static REB_R Event_Actor(struct Reb_Frame *frame_, REBCTX *port, REBCNT action)
{
    REBVAL *spec;
    REBVAL *state;
    REBCNT result;
    REBVAL *arg;
    REBVAL save_port;

    Validate_Port(port, action);

    arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    *D_OUT = *D_ARG(1);

    // Validate and fetch relevant PORT fields:
    state = CTX_VAR(port, STD_PORT_STATE);
    spec  = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec)) fail (Error(RE_INVALID_SPEC, spec));

    // Get or setup internal state data:
    if (!IS_BLOCK(state)) Val_Init_Block(state, Make_Array(127));

    switch (action) {

    case SYM_UPDATE:
        return R_BLANK;

    // Normal block actions done on events:
    case SYM_POKE:
        if (!IS_EVENT(D_ARG(3))) fail (Error_Invalid_Arg(D_ARG(3)));
        goto act_blk;
    case SYM_INSERT:
    case SYM_APPEND:
    //case SYM_PATH:      // not allowed: port/foo is port object field access
    //case SYM_PATH_SET:  // not allowed: above
        if (!IS_EVENT(arg)) fail (Error_Invalid_Arg(arg));
    case SYM_PICK:
act_blk:
        save_port = *D_ARG(1); // save for return
        *D_ARG(1) = *state;
        result = T_Block(ds, action);
        SET_FLAG(Eval_Signals, SIG_EVENT_PORT);
        if (
            action == SYM_INSERT
            || action == SYM_APPEND
            || action == SYM_REMOVE
        ){
            *D_OUT = save_port;
            break;
        }
        return result; // return condition

    case SYM_CLEAR:
        RESET_ARRAY(state);
        CLR_FLAG(Eval_Signals, SIG_EVENT_PORT);
        break;

    case SYM_LENGTH:
        SET_INTEGER(D_OUT, VAL_LEN_HEAD(state));
        break;

    case SYM_OPEN:
        if (!req) { //!!!
            req = OS_MAKE_DEVREQ(RDI_EVENT);
            SET_OPEN(req);
            OS_DO_DEVICE(req, RDC_CONNECT);     // stays queued
        }
        break;

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

    return R_OUT;
}


//
//  Init_Timer_Scheme: C
//
void Init_Timer_Scheme(void)
{
    Register_Scheme(SYM_TIMER, 0, Event_Actor);
}
