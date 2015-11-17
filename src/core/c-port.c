/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  c-port.c
**  Summary: support for I/O ports
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**     See comments in Init_Ports for startup.
**     See www.rebol.net/wiki/Event_System for full details.
**
***********************************************************************/

#include "sys-core.h"

#define MAX_WAIT_MS 64 // Maximum millsec to sleep

//
//  Make_Port: C
// 
// Create a new port. This is done by calling the MAKE_PORT
// function stored in the system/intrinsic object.
//
void Make_Port(REBVAL *out, const REBVAL *spec)
{
    if (Do_Sys_Func_Throws(out, SYS_CTX_MAKE_PORT_P, spec, 0)) {
        // Gave back an unhandled RETURN, BREAK, CONTINUE, etc...
        fail (Error_No_Catch_For_Throw(out));
    }

    // !!! Shouldn't this be testing for !IS_PORT( ) ?
    if (IS_NONE(out)) fail (Error(RE_INVALID_SPEC, spec));
}


//
//  Is_Port_Open: C
// 
// Standard method for checking if port is open.
// A convention. Not all ports use this method.
//
REBFLG Is_Port_Open(REBSER *port)
{
    REBVAL *state = BLK_SKIP(port, STD_PORT_STATE);
    if (!IS_BINARY(state)) return FALSE;
    return IS_OPEN(VAL_BIN_DATA(state));
}


//
//  Set_Port_Open: C
// 
// Standard method for setting a port open/closed.
// A convention. Not all ports use this method.
//
void Set_Port_Open(REBSER *port, REBFLG flag)
{
    REBVAL *state = BLK_SKIP(port, STD_PORT_STATE);
    if (IS_BINARY(state)) {
        if (flag) SET_OPEN(VAL_BIN_DATA(state));
        else SET_CLOSED(VAL_BIN_DATA(state));
    }
}


//
//  Use_Port_State: C
// 
// Use private state area in a port. Create if necessary.
// The size is that of a binary structure used by
// the port for storing internal information.
//
void *Use_Port_State(REBSER *port, REBCNT device, REBCNT size)
{
    REBVAL *state = BLK_SKIP(port, STD_PORT_STATE);

    // If state is not a binary structure, create it:
    if (!IS_BINARY(state)) {
        REBSER *data = Make_Binary(size);
        REBREQ *req = (REBREQ*)STR_HEAD(data);
        req->clen = size;
        CLEAR(STR_HEAD(data), size);
        //data->tail = size; // makes it easier for ACCEPT to clone the port
        SET_FLAG(req->flags, RRF_ALLOC); // not on stack
        req->port = port;
        req->device = device;
        Val_Init_Binary(state, data);
    }

    return (void *)VAL_BIN(state);
}


//
//  Pending_Port: C
// 
// Return TRUE if port value is pending a signal.
// Not valid for all ports - requires request struct!!!
//
REBFLG Pending_Port(REBVAL *port)
{
    REBVAL *state;
    REBREQ *req;

    if (IS_PORT(port)) {
        state = BLK_SKIP(VAL_PORT(port), STD_PORT_STATE);
        if (IS_BINARY(state)) {
            req = (REBREQ*)VAL_BIN(state);
            if (!GET_FLAG(req->flags, RRF_PENDING)) return FALSE;
        }
    }
    return TRUE;
}


//
//  Awake_System: C
// 
// Returns:
//     -1 for errors
//      0 for nothing to do
//      1 for wait is satisifed
//
REBINT Awake_System(REBSER *ports, REBINT only)
{
    REBVAL *port;
    REBVAL *state;
    REBVAL *waked;
    REBVAL *awake;
    REBVAL tmp;
    REBVAL ref_only;
    REBINT result;
    REBVAL out;

    // Get the system port object:
    port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (!IS_PORT(port)) return -10; // verify it is a port object

    // Get wait queue block (the state field):
    state = VAL_OBJ_VALUE(port, STD_PORT_STATE);
    if (!IS_BLOCK(state)) return -10;
    //Debug_Num("S", VAL_TAIL(state));

    // Get waked queue block:
    waked = VAL_OBJ_VALUE(port, STD_PORT_DATA);
    if (!IS_BLOCK(waked)) return -10;

    // If there is nothing new to do, return now:
    if (VAL_TAIL(state) == 0 && VAL_TAIL(waked) == 0) return -1;

    //Debug_Num("A", VAL_TAIL(waked));
    // Get the system port AWAKE function:
    awake = VAL_OBJ_VALUE(port, STD_PORT_AWAKE);
    if (!ANY_FUNC(awake)) return -1;
    if (ports) Val_Init_Block(&tmp, ports);
    else SET_NONE(&tmp);

    if (only) SET_TRUE(&ref_only);
    else SET_NONE(&ref_only);
    // Call the system awake function:
    if (Apply_Func_Throws(&out, awake, port, &tmp, &ref_only, 0))
        fail (Error_No_Catch_For_Throw(&out));

    // Awake function returns 1 for end of WAIT:
    result = (IS_LOGIC(&out) && VAL_LOGIC(&out)) ? 1 : 0;

    return result;
}


//
//  Wait_Ports: C
// 
// Inputs:
//     Ports: a block of ports or zero (on stack to avoid GC).
//     Timeout: milliseconds to wait
// 
// Returns:
//     TRUE when port action happened, or FALSE for timeout.
//
REBINT Wait_Ports(REBSER *ports, REBCNT timeout, REBINT only)
{
    REBI64 base = OS_DELTA_TIME(0, 0);
    REBCNT time;
    REBINT result;
    REBCNT wt = 1;
    REBCNT res = (timeout >= 1000) ? 0 : 16;  // OS dependent?

    while (wt) {
        if (GET_SIGNAL(SIG_ESCAPE)) {
            CLR_SIGNAL(SIG_ESCAPE);
            fail (VAL_ERR_OBJECT(TASK_HALT_ERROR));
        }

        // Process any waiting events:
        if ((result = Awake_System(ports, only)) > 0) return TRUE;

        // If activity, use low wait time, otherwise increase it:
        if (result == 0) wt = 1;
        else {
            wt *= 2;
            if (wt > MAX_WAIT_MS) wt = MAX_WAIT_MS;
        }

        if (timeout != ALL_BITS) {
            // Figure out how long that (and OS_WAIT) took:
            time = (REBCNT)(OS_DELTA_TIME(base, 0)/1000);
            if (time >= timeout) break;   // done (was dt = 0 before)
            else if (wt > timeout - time) // use smaller residual time
                wt = timeout - time;
        }

        //printf("%d %d %d\n", dt, time, timeout);

        // Wait for events or time to expire:
        //Debug_Num("OSW", wt);
        OS_WAIT(wt, res);
    }

    //time = (REBCNT)OS_DELTA_TIME(base, 0);
    //Print("dt: %d", time);

    return FALSE; // timeout
}


//
//  Sieve_Ports: C
// 
// Remove all ports not found in the WAKE list.
// ports could be NULL, in which case the WAKE list is cleared.
//
void Sieve_Ports(REBSER *ports)
{
    REBVAL *port;
    REBVAL *waked;
    REBVAL *val;
    REBCNT n;

    port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (!IS_PORT(port)) return;
    waked = VAL_OBJ_VALUE(port, STD_PORT_DATA);
    if (!IS_BLOCK(waked)) return;

    for (n = 0; ports && n < SERIES_TAIL(ports);) {
        val = BLK_SKIP(ports, n);
        if (IS_PORT(val)) {
            assert(VAL_TAIL(waked) != 0);
            if (VAL_TAIL(waked) == Find_Block_Simple(VAL_SERIES(waked), 0, val)) {//not found
                Remove_Series(ports, n, 1);
                continue;
            }
        }
        n++;
    }
    //clear waked list
    RESET_SERIES(VAL_SERIES(waked));
}


//
//  Find_Action: C
// 
// Given an action number, return the action's index in
// the specified object. If not found, a zero is returned.
//
REBCNT Find_Action(REBVAL *object, REBCNT action)
{
    return Find_Word_Index(
        VAL_OBJ_FRAME(object), Get_Action_Sym(action), FALSE
    );
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
int Do_Port_Action(struct Reb_Call *call_, REBSER *port, REBCNT action)
{
    REBVAL *actor;
    REBCNT n = 0;

    assert(action < A_MAX_ACTION);

    // Verify valid port (all of these must be false):
    if (
        // Must be = or larger than std port:
        (SERIES_TAIL(port) < STD_PORT_MAX) ||
        // Must be an object series:
        !IS_FRAME(BLK_HEAD(port)) ||
        // Must have a spec object:
        !IS_OBJECT(BLK_SKIP(port, STD_PORT_SPEC))
    ) {
        fail (Error(RE_INVALID_PORT));
    }

    // Get actor for port, if it has one:
    actor = BLK_SKIP(port, STD_PORT_ACTOR);

    if (IS_NONE(actor)) return R_NONE;

    // If actor is a native function:
    if (IS_NATIVE(actor))
        return cast(REBPAF, VAL_FUNC_CODE(actor))(call_, port, action);

    // actor must be an object:
    if (!IS_OBJECT(actor)) fail (Error(RE_INVALID_ACTOR));

    // Dispatch object function:
    n = Find_Action(actor, action);
    actor = Obj_Value(actor, n);
    if (!n || !actor || !ANY_FUNC(actor)) {
        REBVAL action_word;
        Val_Init_Word_Unbound(&action_word, REB_WORD, Get_Action_Sym(action));

        fail (Error(RE_NO_PORT_ACTION, &action_word));
    }

    if (Redo_Func_Throws(actor)) {
        // The throw name will be in D_OUT, with thrown value in task vars
        return R_OUT_IS_THROWN;
    }

    return R_OUT;

    // If not in PORT actor, use the SCHEME actor:
#ifdef no_longer_used
    if (n == 0) {
        actor = Obj_Value(scheme, STD_SCHEME_actor);
        if (!actor) goto err;
        if (IS_NATIVE(actor)) goto fun;
        if (!IS_OBJECT(actor)) goto err; //vTrap_Expect(value, STD_PORT_actor, REB_OBJECT);
        n = Find_Action(actor, action);
        if (n == 0) goto err;
    }
#endif

}


//
//  Secure_Port: C
// 
// kind: word that represents the type (e.g. 'file)
// req:  I/O request
// name: value that holds the original user spec
// path: the local path to compare with
//
void Secure_Port(REBCNT kind, REBREQ *req, REBVAL *name, REBSER *path)
{
    REBYTE *flags;
    REBVAL val;

    Val_Init_String(&val, path);
    flags = Security_Policy(kind, &val); // policy flags

    // Check policy integer:
    // Mask is [xxxx wwww rrrr] - each holds the action
    if (GET_FLAG(req->modes, RFM_READ))  Trap_Security(flags[POL_READ], kind, name);
    if (GET_FLAG(req->modes, RFM_WRITE)) Trap_Security(flags[POL_WRITE], kind, name);
}


//
//  Validate_Port: C
// 
// Because port actors are exposed to the user level, we must
// prevent them from being called with invalid values.
//
void Validate_Port(REBSER *port, REBCNT action)
{
    if (
        action >= A_MAX_ACTION
        || port->tail > 50
        || SERIES_WIDE(port) != sizeof(REBVAL)
        || !IS_FRAME(BLK_HEAD(port))
        || !IS_OBJECT(BLK_SKIP(port, STD_PORT_SPEC))
    ) {
        fail (Error(RE_INVALID_PORT));
    }
}

/***********************************************************************
**
**  Scheme Native Action Support
**
**      This array is used to associate a scheme word with its
**      native action functions.
**
**      Each native port scheme must be listed here. This list is
**      created by each native scheme calling Register_Scheme()
**      during initialization.
**
**  Example of defining actions:
**
**      static const PORT_ACTION File_Actions[] = {
**          A_OPEN,     P_open,
**          A_CLOSE,    P_close,
**          0, 0
**      }
**
**      Register_Scheme(SYM_FILE, &File_Actions[0], 0);
**
**
***********************************************************************/

#ifdef HAS_POSIX_SIGNAL
#define MAX_SCHEMES 12      // max native schemes
#else
#define MAX_SCHEMES 11      // max native schemes
#endif

typedef struct rebol_scheme_actions {
    REBCNT sym;
    const PORT_ACTION *map;
    REBPAF fun;
} SCHEME_ACTIONS;

SCHEME_ACTIONS *Scheme_Actions; // Initial Global (not threaded)


//
//  Register_Scheme: C
// 
// Associate a scheme word (e.g. FILE) with a set of native
// scheme actions. This will be used by the Set_Scheme native
//
void Register_Scheme(REBCNT sym, const PORT_ACTION *map, REBPAF fun)
{
    REBINT n;

    for (n = 0; n < MAX_SCHEMES && Scheme_Actions[n].sym; n++);
    assert(n < MAX_SCHEMES);

    Scheme_Actions[n].sym = sym;
    Scheme_Actions[n].map = map;
    Scheme_Actions[n].fun = fun;
}


//
//  set-scheme: native [
//  
//  "Low-level port scheme actor initialization."
//  
//      scheme [object!]
//  ]
//
REBNATIVE(set_scheme)
{
    REBVAL *scheme;
    REBVAL *actor;
    REBVAL *func;
    REBVAL *act;
    REBCNT n;
    const PORT_ACTION *map = 0;

    scheme = D_ARG(1);

    act = Obj_Value(scheme, STD_SCHEME_NAME);
    if (!IS_WORD(act)) return R_NONE;
    actor = Obj_Value(scheme, STD_SCHEME_ACTOR);
    if (!actor) return R_NONE;

    // Does this scheme have native actor or actions?
    for (n = 0; n < MAX_SCHEMES && Scheme_Actions[n].sym; n++) {
        if (Scheme_Actions[n].sym == VAL_WORD_SYM(act)) break;
    }
    if (n == MAX_SCHEMES || !Scheme_Actions[n].sym) return R_NONE;

    // The scheme uses a native actor:
    if (Scheme_Actions[n].fun) {
        // Hand build a native function used to reach native scheme actors.
        REBSER *ser = Make_Array(1);
        act = Alloc_Tail_Array(ser);

        Val_Init_Typeset(
            act,
            // Typeset is chosen as REB_END to prevent normal invocation;
            // these actors are only dispatched from the C code.
            FLAGIT_64(REB_END),
            // !!! Because "any word will do", it's just making an args list
            // that looks like [port!]
            SYM_FROM_KIND(REB_PORT)
        );

        // !!! Review: If this spec ever got leaked then it would be leaking
        // 'typed' words to the user.  For safety, a single global actor spec
        // could be made at startup.
        VAL_FUNC_SPEC(actor) = ser;
        VAL_FUNC_PARAMLIST(actor) = ser;
        MANAGE_SERIES(ser);

        VAL_FUNC_CODE(actor) = (REBFUN)(Scheme_Actions[n].fun);

        VAL_SET(actor, REB_NATIVE);
        return R_TRUE;
    }

    // The scheme has an array of action natives:
    if (!IS_OBJECT(actor)) return R_NONE;

    // Map action natives to scheme actor words:
    map = Scheme_Actions[n].map;
    for (; map->func; map++) {
        // Find the action in the scheme actor:
        n = Find_Action(actor, map->action);
        if (n) {
            // Get standard action's spec block:
            act = Get_Action_Value(map->action);

            // Make native function for action:
            func = Obj_Value(actor, n); // function
            Make_Native(func, VAL_FUNC_SPEC(act), (REBFUN)(map->func), REB_NATIVE);
        }
    }
    return R_TRUE;
}


//
//  Init_Ports: C
// 
// Initialize port scheme related subsystems.
// 
// In order to add a port scheme:
// 
// In mezz-ports.r add a make-scheme.
// Add an Init_*_Scheme() here.
// Be sure host-devices.c has the device enabled.
//
void Init_Ports(void)
{
    Scheme_Actions = ALLOC_ARRAY(SCHEME_ACTIONS, MAX_SCHEMES);
    CLEAR(Scheme_Actions, MAX_SCHEMES * sizeof(SCHEME_ACTIONS));

    Init_Console_Scheme();
    Init_File_Scheme();
    Init_Dir_Scheme();
    Init_Event_Scheme();
    Init_TCP_Scheme();
    Init_UDP_Scheme();
    Init_DNS_Scheme();

#ifdef TO_WINDOWS
    Init_Clipboard_Scheme();
#endif

#if defined(TO_LINUX) || defined(TO_WINDOWS)
    Init_Serial_Scheme();
#endif

#ifdef HAS_POSIX_SIGNAL
    Init_Signal_Scheme();
#endif
}


//
//  Shutdown_Ports: C
//
void Shutdown_Ports(void)
{
    FREE_ARRAY(SCHEME_ACTIONS, MAX_SCHEMES, Scheme_Actions);
}
