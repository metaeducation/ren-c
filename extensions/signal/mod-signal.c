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

static int sig_word_num(const REBVAL *word)
{
    return rebUnboxInteger("select just", word, "[",
        "sigalrm", rebI(SIGALRM),
        "sigabrt", rebI(SIGABRT),
        "sigbus", rebI(SIGBUS),
        "sigchld", rebI(SIGCHLD),
        "sigcont", rebI(SIGCONT),
        "sigfpe", rebI(SIGFPE),
        "sighup", rebI(SIGHUP),
        "sigill", rebI(SIGILL),
        "sigint", rebI(SIGINT),
        // SIGKILL can't be caught
        "sigpipe", rebI(SIGPIPE),
        "sigquit", rebI(SIGQUIT),
        "sigsegv", rebI(SIGSEGV),
        // SIGSTOP can't be caught
        "sigterm", rebI(SIGTERM),
        "sigttin", rebI(SIGTTIN),
        "sigttou", rebI(SIGTTOU),
        "sigusr1", rebI(SIGUSR1),
        "sigusr2", rebI(SIGUSR2),
        "sigtstp", rebI(SIGTSTP),
        "sigpoll", rebI(SIGPOLL),
        "sigprof", rebI(SIGPROF),
        "sigsys", rebI(SIGSYS),
        "sigurg", rebI(SIGURG),
        "sigvtalrm", rebI(SIGVTALRM),
        "sigxcpu", rebI(SIGXCPU),
        "sigxfsz", rebI(SIGXFSZ),
        "fail [{Unknown SIG:} just", word, "]",
    "]");
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
        switch (VAL_WORD_ID(verb)) {
        case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(value));
            SYMID property = VAL_WORD_ID(ARG(property));

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

            const RELVAL *tail;
            const RELVAL *item = VAL_ARRAY_AT(&tail, val);
            for (; item != tail; ++item) {
                DECLARE_LOCAL (sig);
                Derelativize(sig, item, VAL_SPECIFIER(val));

                if (not IS_WORD(sig))
                    fail (Error_Invalid_Spec_Raw(sig));

                if (rebDid("@", sig, "== 'all")) {
                    if (sigfillset(&ReqPosixSignal(signal)->mask) < 0)
                        fail (Error_Invalid_Spec_Raw(sig));
                    break;
                }

                if (
                    sigaddset(
                        &ReqPosixSignal(signal)->mask,
                        sig_word_num(sig)
                    ) < 0
                ){
                    fail (Error_Invalid_Spec_Raw(sig));
                }
            }

            OS_DO_DEVICE_SYNC(signal, RDC_OPEN);

            if (VAL_WORD_ID(verb) == SYM_OPEN)
                RETURN (port);

            assert((req->flags & RRF_OPEN) and VAL_WORD_ID(verb) == SYM_READ);
            break; } // fallthrough

        case SYM_CLOSE:
            return D_OUT;

        case SYM_ON_WAKE_UP:
            break; // fallthrough (allowed after a close)

        default:
            fail (Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

    switch (VAL_WORD_ID(verb)) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        SYMID property = VAL_WORD_ID(ARG(property));

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
        return Init_None(D_OUT); }

    case SYM_READ: {
        // This device is opened on the READ:
        // Issue the read request:
        REBVAL *arg = CTX_VAR(ctx, STD_PORT_DATA);

        size_t len = req->length = 8;
        REBBIN *bin = Make_Binary(len * sizeof(siginfo_t));
        req->common.data = BIN_HEAD(bin);

        OS_DO_DEVICE_SYNC(signal, RDC_READ);

        arg = CTX_VAR(ctx, STD_PORT_DATA);
        if (!IS_BLOCK(arg))
            Init_Block(arg, Make_Array(len));

        len = req->actual;

        if (len <= 0) {
            Free_Unmanaged_Series(bin);
            return nullptr;
        }

        update(signal, len, arg);
        Free_Unmanaged_Series(bin);
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


//
//  export shutdown*: native [
//
//  {Shut down the signal device}
//
//      return: <none>
//  ]
//
REBNATIVE(shutdown_p)
{
    OS_Unregister_Device(&Dev_Signal);

    return rebNone();
}
