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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
REBREQ *Ensure_Port_State(Value* port, REBLEN device)
{
    assert(device < RDI_MAX);

    REBDEV *dev = Devices[device];
    if (not dev)
        return nullptr;

    REBCTX *ctx = VAL_CONTEXT(port);
    Value* state = CTX_VAR(ctx, STD_PORT_STATE);
    REBLEN req_size = dev->req_size;

    if (!Is_Binary(state)) {
        assert(IS_NULLED(state));
        Blob* data = Make_Blob(req_size);
        CLEAR(Blob_Head(data), req_size);
        Term_Blob_Len(data, req_size);

        REBREQ *req = cast(REBREQ*, Blob_Head(data));
        req->port_ctx = ctx;
        req->device = device;
        Init_Binary(state, data);
    }
    else {
        assert(VAL_INDEX(state) == 0); // should always be at head
        assert(VAL_LEN_HEAD(state) == req_size); // should be right size
    }

    return cast(REBREQ*, Cell_Binary_Head(state));
}


//
//  Pending_Port: C
//
// Return true if port value is pending a signal.
// Not valid for all ports - requires request struct!!!
//
bool Pending_Port(Value* port)
{
    Value* state;
    REBREQ *req;

    if (Is_Port(port)) {
        state = CTX_VAR(VAL_CONTEXT(port), STD_PORT_STATE);
        if (Is_Binary(state)) {
            req = cast(REBREQ*, Cell_Binary_Head(state));
            if (not (req->flags & RRF_PENDING))
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
REBINT Awake_System(Array* ports, bool only)
{
    // Get the system port object:
    Value* port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (!Is_Port(port))
        return -10; // verify it is a port object

    // Get wait queue block (the state field):
    Value* state = VAL_CONTEXT_VAR(port, STD_PORT_STATE);
    if (!Is_Block(state))
        return -10;

    // Get waked queue block:
    Value* waked = VAL_CONTEXT_VAR(port, STD_PORT_DATA);
    if (!Is_Block(waked))
        return -10;

    // If there is nothing new to do, return now:
    if (VAL_LEN_HEAD(state) == 0 and VAL_LEN_HEAD(waked) == 0)
        return -1;

    // Get the system port AWAKE function:
    Value* awake = VAL_CONTEXT_VAR(port, STD_PORT_AWAKE);
    if (not Is_Action(awake))
        return -1;

    DECLARE_VALUE (tmp);
    if (ports)
        Init_Block(tmp, ports);
    else
        Init_Blank(tmp);

    DECLARE_VALUE (awake_only);
    if (only) {
        //
        // If we're using /ONLY, we need path AWAKE/ONLY to call.  (Ren-C's
        // va_list API does not support positionally-provided refinements.)
        //
        Array* a = Make_Array(2);
        Append_Value(a, awake);
        Init_Word(Alloc_Tail_Array(a), Canon(SYM_ONLY));

        Init_Path(awake_only, a);
    }

    // Call the system awake function:
    //
    DECLARE_VALUE (result);
    if (Apply_Only_Throws(
        result,
        true, // fully
        only ? awake_only : awake,
        port,
        tmp,
        rebEND
    )) {
        fail (Error_No_Catch_For_Throw(result));
    }

    // Awake function returns 1 for end of WAIT:
    //
    return (Is_Logic(result) and VAL_LOGIC(result)) ? 1 : 0;
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
    Value* out,
    Array* ports,
    REBLEN timeout,
    bool only
){
    REBI64 base = OS_DELTA_TIME(0);
    REBLEN time;
    REBLEN wt = 1;
    REBLEN res = (timeout >= 1000) ? 0 : 16;  // OS dependent?

    // Waiting opens the doors to pressing Ctrl-C, which may get this code
    // to throw an error.  There needs to be a state to catch it.
    //
    assert(Saved_State != nullptr);

    while (wt) {
        if (GET_SIGNAL(SIG_HALT)) {
            CLR_SIGNAL(SIG_HALT);

            Copy_Cell(out, NAT_VALUE(halt));
            CONVERT_NAME_TO_THROWN(out, NULLED_CELL);
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
            Copy_Cell(out, TRUE_VALUE); // port action happened
            return false; // not thrown
        }

        // If activity, use low wait time, otherwise increase it:
        if (ret == 0) wt = 1;
        else {
            wt *= 2;
            if (wt > MAX_WAIT_MS) wt = MAX_WAIT_MS;
        }
        Value* pump = Get_System(SYS_PORTS, PORTS_PUMP);
        if (not Is_Block(pump))
            fail ("system/ports/pump must be a block");

        DECLARE_VALUE (result);
        if (Do_Any_Array_At_Throws(result, pump))
            fail (Error_No_Catch_For_Throw(result));

        if (timeout != ALL_BITS) {
            // Figure out how long that (and OS_WAIT) took:
            time = cast(REBLEN, OS_DELTA_TIME(base) / 1000);
            if (time >= timeout) break;   // done (was dt = 0 before)
            else if (wt > timeout - time) // use smaller residual time
                wt = timeout - time;
        }

        //printf("%d %d %d\n", dt, time, timeout);

        // Wait for events or time to expire:
        OS_WAIT(wt, res);
    }

    //time = (REBLEN)OS_DELTA_TIME(base);
    //Print("dt: %d", time);

    Copy_Cell(out, FALSE_VALUE); // timeout;
    return false; // not thrown
}


//
//  Sieve_Ports: C
//
// Remove all ports not found in the WAKE list.
// ports could be nullptr, in which case the WAKE list is cleared.
//
void Sieve_Ports(Array* ports)
{
    Value* port;
    Value* waked;
    REBLEN n;

    port = Get_System(SYS_PORTS, PORTS_SYSTEM);
    if (!Is_Port(port)) return;
    waked = VAL_CONTEXT_VAR(port, STD_PORT_DATA);
    if (!Is_Block(waked)) return;

    for (n = 0; ports and n < Array_Len(ports);) {
        Cell* val = Array_At(ports, n);
        if (Is_Port(val)) {
            assert(VAL_LEN_HEAD(waked) != 0);
            if (
                Find_In_Array_Simple(Cell_Array(waked), 0, val)
                == VAL_LEN_HEAD(waked) // `=len` means not found
            ) {
                Remove_Flex(ports, n, 1);
                continue;
            }
        }
        n++;
    }
    //clear waked list
    RESET_ARRAY(Cell_Array(waked));
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
// the target function and refinements, passing args with DO_FLAG_EVAL_ONLY.
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
bool Redo_Action_Throws(Level* L, REBACT *run)
{
    Array* code_arr = Make_Array(Level_Num_Args(L)); // max, e.g. no refines
    Cell* code = Array_Head(code_arr);

    // The first element of our path will be the ACTION!, followed by its
    // refinements...which in the worst case, all args will be refinements:
    //
    Array* path_arr = Make_Array(Level_Num_Args(L) + 1);
    Cell* path = Array_Head(path_arr);
    Init_Action_Unbound(path, run); // !!! What if there's a binding?
    ++path;

    assert(IS_END(L->param)); // okay to reuse, if it gets put back...
    L->param = ACT_PARAMS_HEAD(Level_Phase(L));
    L->arg = Level_Args_Head(L);
    L->special = ACT_SPECIALTY_HEAD(Level_Phase(L));

    bool ignoring = false;

    for (; NOT_END(L->param); ++L->param, ++L->arg, ++L->special) {
        if (Is_Param_Hidden(L->param))
            continue; // !!! is this still relevant?
        if (GET_VAL_FLAG(L->special, ARG_MARKED_CHECKED))
            continue; // a parameter that was "specialized out" of this phase

        enum Reb_Param_Class pclass = VAL_PARAM_CLASS(L->param);

        if (
            pclass == PARAM_CLASS_LOCAL
            or pclass == PARAM_CLASS_RETURN
        ){
             continue; // don't add a callsite expression for it (can't)!
        }

        if (pclass == PARAM_CLASS_REFINEMENT) {
            if (Is_Blank(L->arg)) {
                ignoring = true; // don't add to PATH!
                continue;
            }

            assert(Is_Refinement(L->arg));
            ignoring = false;
            Init_Word(path, Cell_Parameter_Symbol(L->param));
            ++path;
            continue;
        }

        if (ignoring)
            continue;

        Copy_Cell(code, L->arg);
        ++code;
    }

    Term_Array_Len(code_arr, code - Array_Head(code_arr));
    Manage_Flex(code_arr);

    DECLARE_VALUE (first);
    Term_Array_Len(path_arr, path - Array_Head(path_arr));
    Init_Path(first, path_arr);
    SET_VAL_FLAG(first, VALUE_FLAG_EVAL_FLIP); // make the PATH! invoke action

    // Invoke DO with the special mode requesting non-evaluation on all
    // args, as they were evaluated the first time around.
    //
    REBIXO indexor = Eval_Array_At_Core(
        SET_END(L->out),
        first, // path not in array, will be "virtual" first element
        code_arr,
        0, // index
        SPECIFIED, // reusing existing Value arguments, no relative cells
        DO_FLAG_EXPLICIT_EVALUATE // DON'T double-evaluate arguments
            | DO_FLAG_NO_RESIDUE // raise an error if all args not consumed
    );

    if (IS_END(L->out))
        fail ("Redo_Action_Throws() was either empty or all COMMENTs/ELIDEs");

    return indexor == THROWN_FLAG;
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
REB_R Do_Port_Action(Level* level_, Value* port, Value* verb)
{
    FAIL_IF_BAD_PORT(port);

    REBCTX *ctx = VAL_CONTEXT(port);
    Value* actor = CTX_VAR(ctx, STD_PORT_ACTOR);

    REB_R r;

    // If actor is a HANDLE!, it should be a PAF
    //
    // !!! Review how user-defined types could make this better/safer, as if
    // it's some other kind of handle value this could crash.
    //
    if (Is_Native_Port_Actor(actor)) {
        r = cast(PORT_HOOK, VAL_HANDLE_CFUNC(actor))(level_, port, verb);
        goto post_process_output;
    }

    if (not Is_Object(actor))
        fail (Error_Invalid_Actor_Raw());

    // Dispatch object function:

    REBLEN n; // goto would cross initialization
    n = Find_Canon_In_Context(
        VAL_CONTEXT(actor),
        VAL_WORD_CANON(verb),
        false // !always
    );

    Value* action;
    if (n == 0 or not Is_Action(action = VAL_CONTEXT_VAR(actor, n)))
        fail (Error_No_Port_Action_Raw(verb));

    if (Redo_Action_Throws(level_, VAL_ACTION(action)))
        return R_THROWN;

    r = OUT; // result should be in level_->out

    // !!! READ's /LINES and /STRING refinements are something that should
    // work regardless of data source.  But R3-Alpha only implemented it in
    // %p-file.c, so it got ignored.  Ren-C caught that it was being ignored,
    // so the code was moved to here as a quick fix.
    //
    // !!! Note this code is incorrect for files read in chunks!!!

post_process_output:
    if (Cell_Word_Id(verb) == SYM_READ) {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));
        UNUSED(PAR(part));
        UNUSED(PAR(limit));
        UNUSED(PAR(seek));
        UNUSED(PAR(index));

        assert(r == OUT);

        if ((REF(string) or REF(lines)) and not Is_Text(OUT)) {
            if (not Is_Binary(OUT))
                fail ("/STRING or /LINES used on a non-BINARY!/STRING! read");

            Flex* decoded = Make_Sized_String_UTF8(
                cs_cast(Cell_Binary_At(OUT)),
                Cell_Series_Len_At(OUT)
            );
            Init_Text(OUT, decoded);
        }

        if (REF(lines)) { // caller wants a BLOCK! of STRING!s, not one string
            assert(Is_Text(OUT));

            DECLARE_VALUE (temp);
            Copy_Cell(temp, OUT);
            Init_Block(OUT, Split_Lines(temp));
        }
    }

    return r;
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
void Make_Port_Actor_Handle(Value* out, PORT_HOOK paf)
{
    Init_Handle_Cfunc(out, cast(CFUNC*, paf), 0);
}
