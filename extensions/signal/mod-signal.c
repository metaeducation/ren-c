//
//  File: %p-signal.c
//  Summary: "signal port interface"
//  Section: ports
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2017 Ren-C Open Source Contributors
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

#include "sys-core.h"

#include "tmp-mod-signal.h"

#include <signal.h>  // !!! Was #include <sys/signal.h>, caused warning

#include "signal-req.h"

static void update(REBREQ *signal, REBINT len, REBVAL *arg)
{
    struct rebol_devreq *req = Req(signal);

    const siginfo_t *sig = cast(siginfo_t *, req->common.data);
    int i = 0;
    const REBYTE signal_no[] = "signal-no";
    const REBYTE code[] = "code";
    const REBYTE source_pid[] = "source-pid";
    const REBYTE source_uid[] = "source-uid";

    Extend_Series(VAL_SERIES_KNOWN_MUTABLE(arg), len);

    for (i = 0; i < len; i ++) {
        REBCTX *obj = Alloc_Context(REB_OBJECT, 8);
        REBVAL *val = Append_Context(
            obj, nullptr, Intern_UTF8_Managed(signal_no, strsize(signal_no))
        );
        Init_Integer(val, sig[i].si_signo);

        val = Append_Context(
            obj, nullptr, Intern_UTF8_Managed(code, strsize(code))
        );
        Init_Integer(val, sig[i].si_code);
        val = Append_Context(
            obj, nullptr, Intern_UTF8_Managed(source_pid, strsize(source_pid))
        );
        Init_Integer(val, sig[i].si_pid);
        val = Append_Context(
            obj, nullptr, Intern_UTF8_Managed(source_uid, strsize(source_uid))
        );
        Init_Integer(val, sig[i].si_uid);

        Init_Object(Alloc_Tail_Array(VAL_ARRAY_KNOWN_MUTABLE(arg)), obj);
    }

    req->actual = 0; /* avoid duplicate updates */
}

static int sig_word_num(const REBSTR *canon)
{
    switch (STR_SYMBOL(canon)) {
        case SYM_SIGALRM:
            return SIGALRM;
        case SYM_SIGABRT:
            return SIGABRT;
        case SYM_SIGBUS:
            return SIGBUS;
        case SYM_SIGCHLD:
            return SIGCHLD;
        case SYM_SIGCONT:
            return SIGCONT;
        case SYM_SIGFPE:
            return SIGFPE;
        case SYM_SIGHUP:
            return SIGHUP;
        case SYM_SIGILL:
            return SIGILL;
        case SYM_SIGINT:
            return SIGINT;
/* can't be caught
        case SYM_SIGKILL:
            return SIGKILL;
*/
        case SYM_SIGPIPE:
            return SIGPIPE;
        case SYM_SIGQUIT:
            return SIGQUIT;
        case SYM_SIGSEGV:
            return SIGSEGV;
/* can't be caught
        case SYM_SIGSTOP:
            return SIGSTOP;
*/
        case SYM_SIGTERM:
            return SIGTERM;
        case SYM_SIGTTIN:
            return SIGTTIN;
        case SYM_SIGTTOU:
            return SIGTTOU;
        case SYM_SIGUSR1:
            return SIGUSR1;
        case SYM_SIGUSR2:
            return SIGUSR2;
        case SYM_SIGTSTP:
            return SIGTSTP;
        case SYM_SIGPOLL:
            return SIGPOLL;
        case SYM_SIGPROF:
            return SIGPROF;
        case SYM_SIGSYS:
            return SIGSYS;
        case SYM_SIGTRAP:
            return SIGTRAP;
        case SYM_SIGURG:
            return SIGURG;
        case SYM_SIGVTALRM:
            return SIGVTALRM;
        case SYM_SIGXCPU:
            return SIGXCPU;
        case SYM_SIGXFSZ:
            return SIGXFSZ;
        default: {
            DECLARE_LOCAL (word);
            Init_Word(word, canon);

            fail (Error_Invalid_Spec_Raw(word));
        }
    }
}

//
//  Signal_Actor: C
//
static REB_R Signal_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
{
    REBREQ *signal = Force_Get_Port_State(port, &Dev_Signal);
    struct rebol_devreq *req = Req(signal);

    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    if (not (req->flags & RRF_OPEN)) {
        switch (VAL_WORD_SYM(verb)) {
        case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(value));
            REBSYM property = VAL_WORD_SYM(ARG(property));

            switch (property) {
            case SYM_OPEN_Q:
                return Init_False(D_OUT);

            default:
                break;
            }

            fail (Error_On_Port(SYM_NOT_OPEN, port, -12)); }

        case SYM_READ:
        case SYM_OPEN: {
            REBVAL *val = Obj_Value(spec, STD_PORT_SPEC_SIGNAL_MASK);
            if (!IS_BLOCK(val))
                fail (Error_Invalid_Spec_Raw(val));

            sigemptyset(&ReqPosixSignal(signal)->mask);

            const RELVAL *item;
            for (item = VAL_ARRAY_AT_HEAD(val, 0); NOT_END(item); ++item) {
                DECLARE_LOCAL (sig);
                Derelativize(sig, item, VAL_SPECIFIER(val));

                if (not IS_WORD(sig))
                    fail (Error_Invalid_Spec_Raw(sig));

                if (VAL_WORD_SYM(sig) == SYM_ALL) {
                    if (sigfillset(&ReqPosixSignal(signal)->mask) < 0)
                        fail (Error_Invalid_Spec_Raw(sig));
                    break;
                }

                if (
                    sigaddset(
                        &ReqPosixSignal(signal)->mask,
                        sig_word_num(VAL_WORD_CANON(sig))
                    ) < 0
                ){
                    fail (Error_Invalid_Spec_Raw(sig));
                }
            }

            OS_DO_DEVICE_SYNC(signal, RDC_OPEN);

            if (VAL_WORD_SYM(verb) == SYM_OPEN)
                RETURN (port);

            assert((req->flags & RRF_OPEN) and VAL_WORD_SYM(verb) == SYM_READ);
            break; } // fallthrough

        case SYM_CLOSE:
            return D_OUT;

        case SYM_ON_WAKE_UP:
            break; // fallthrough (allowed after a close)

        default:
            fail (Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

    switch (VAL_WORD_SYM(verb)) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));

        switch (property) {
        case SYM_OPEN_Q:
            return Init_True(D_OUT);

        default:
            break;
        }

        break; }

    case SYM_ON_WAKE_UP: {
        //
        // Update the port object after a READ or WRITE operation.
        // This is normally called by the WAKE-UP function.
        //
        REBVAL *arg = CTX_VAR(ctx, STD_PORT_DATA);
        if (req->command == RDC_READ) {
            REBINT len = req->actual;
            if (len > 0) {
                update(signal, len, arg);
            }
        }
        return Init_Void(D_OUT); }

    case SYM_READ: {
        // This device is opened on the READ:
        // Issue the read request:
        REBVAL *arg = CTX_VAR(ctx, STD_PORT_DATA);

        size_t len = req->length = 8;
        REBSER *ser = Make_Binary(len * sizeof(siginfo_t));
        req->common.data = BIN_HEAD(ser);

        OS_DO_DEVICE_SYNC(signal, RDC_READ);

        arg = CTX_VAR(ctx, STD_PORT_DATA);
        if (!IS_BLOCK(arg))
            Init_Block(arg, Make_Array(len));

        len = req->actual;

        if (len <= 0) {
            Free_Unmanaged_Series(ser);
            return nullptr;
        }

        update(signal, len, arg);
        Free_Unmanaged_Series(ser);
        RETURN (port); }

    case SYM_CLOSE: {
        OS_DO_DEVICE_SYNC(signal, RDC_CLOSE);
        RETURN (port); }

    case SYM_OPEN:
        fail (Error_Already_Open_Raw(port));

    default:
        break;
    }

    return R_UNHANDLED;
}


//
//  export get-signal-actor-handle: native [
//
//  {Retrieve handle to the native actor for POSIX signals}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_signal_actor_handle)
{
    OS_Register_Device(&Dev_Signal);

    Make_Port_Actor_Handle(D_OUT, &Signal_Actor);
    return D_OUT;
}
