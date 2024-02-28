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
// Copyright 2014-2017 Rebol Open Source Contributors
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

#include "sys-core.h"

#ifdef HAS_POSIX_SIGNAL
#include <sys/signal.h>

static void update(struct devreq_posix_signal *signal, REBINT len, Value* arg)
{
    REBREQ *req = AS_REBREQ(signal);
    const siginfo_t *sig = cast(siginfo_t *, req->common.data);
    int i = 0;
    const REBYTE signal_no[] = "signal-no";
    const REBYTE code[] = "code";
    const REBYTE source_pid[] = "source-pid";
    const REBYTE source_uid[] = "source-uid";

    Extend_Series(VAL_SERIES(arg), len);

    for (i = 0; i < len; i ++) {
        REBCTX *obj = Alloc_Context(REB_OBJECT, 8);
        Value* val = Append_Context(
            obj, NULL, Intern_UTF8_Managed(signal_no, LEN_BYTES(signal_no))
        );
        Init_Integer(val, sig[i].si_signo);

        val = Append_Context(
            obj, NULL, Intern_UTF8_Managed(code, LEN_BYTES(code))
        );
        Init_Integer(val, sig[i].si_code);
        val = Append_Context(
            obj, NULL, Intern_UTF8_Managed(source_pid, LEN_BYTES(source_pid))
        );
        Init_Integer(val, sig[i].si_pid);
        val = Append_Context(
            obj, NULL, Intern_UTF8_Managed(source_uid, LEN_BYTES(source_uid))
        );
        Init_Integer(val, sig[i].si_uid);

        Init_Object(Alloc_Tail_Array(VAL_ARRAY(arg)), obj);
    }

    req->actual = 0; /* avoid duplicate updates */
}

static int sig_word_num(REBSTR *canon)
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
static REB_R Signal_Actor(REBFRM *frame_, Value* port, Value* verb)
{
    REBREQ *req = Ensure_Port_State(port, RDI_SIGNAL);
    struct devreq_posix_signal *signal = DEVREQ_POSIX_SIGNAL(req);

    REBCTX *ctx = VAL_CONTEXT(port);
    Value* spec = CTX_VAR(ctx, STD_PORT_SPEC);

    if (not (req->flags & RRF_OPEN)) {
        switch (Cell_Word_Id(verb)) {
        case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(value));
            Option(SymId) property = Cell_Word_Id(ARG(property));

            switch (property) {
            case SYM_OPEN_Q:
                return Init_False(D_OUT);

            default:
                break;
            }

            fail (Error_On_Port(SYM_NOT_OPEN, port, -12)); }

        case SYM_READ:
        case SYM_OPEN: {
            Value* val = Obj_Value(spec, STD_PORT_SPEC_SIGNAL_MASK);
            if (!IS_BLOCK(val))
                fail (Error_Invalid_Spec_Raw(val));

            sigemptyset(&signal->mask);

            Cell* item;
            for (item = VAL_ARRAY_AT_HEAD(val, 0); NOT_END(item); ++item) {
                DECLARE_LOCAL (sig);
                Derelativize(sig, item, VAL_SPECIFIER(val));

                if (not IS_WORD(sig))
                    fail (Error_Invalid_Spec_Raw(sig));

                if (Cell_Word_Id(sig) == SYM_ALL) {
                    if (sigfillset(&signal->mask) < 0)
                        fail (Error_Invalid_Spec_Raw(sig));
                    break;
                }

                if (
                    sigaddset(
                        &signal->mask,
                        sig_word_num(VAL_WORD_CANON(sig))
                    ) < 0
                ){
                    fail (Error_Invalid_Spec_Raw(sig));
                }
            }

            OS_DO_DEVICE_SYNC(req, RDC_OPEN);

            if (Cell_Word_Id(verb) == SYM_OPEN)
                RETURN (port);

            assert((req->flags & RRF_OPEN) and Cell_Word_Id(verb) == SYM_READ);
            break; } // fallthrough

        case SYM_CLOSE:
            return D_OUT;

        case SYM_ON_WAKE_UP:
            break; // fallthrough (allowed after a close)

        default:
            fail (Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

    switch (Cell_Word_Id(verb)) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        Option(SymId) property = Cell_Word_Id(ARG(property));

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
        Value* arg = CTX_VAR(ctx, STD_PORT_DATA);
        if (req->command == RDC_READ) {
            REBINT len = req->actual;
            if (len > 0) {
                update(signal, len, arg);
            }
        }
        return Init_Bar(D_OUT); }

    case SYM_READ: {
        // This device is opened on the READ:
        // Issue the read request:
        Value* arg = CTX_VAR(ctx, STD_PORT_DATA);

        size_t len = req->length = 8;
        REBSER *ser = Make_Binary(len * sizeof(siginfo_t));
        req->common.data = BIN_HEAD(ser);

        OS_DO_DEVICE_SYNC(req, RDC_READ);

        arg = CTX_VAR(ctx, STD_PORT_DATA);
        if (!IS_BLOCK(arg))
            Init_Block(arg, Make_Arr(len));

        len = req->actual;

        if (len <= 0) {
            Free_Unmanaged_Series(ser);
            return nullptr;
        }

        update(signal, len, arg);
        Free_Unmanaged_Series(ser);
        RETURN (port); }

    case SYM_CLOSE: {
        OS_DO_DEVICE_SYNC(req, RDC_CLOSE);
        RETURN (port); }

    case SYM_OPEN:
        fail (Error_Already_Open_Raw(port));

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));
}

#endif //HAS_POSIX_SIGNAL


//
//  get-signal-actor-handle: native [
//
//  {Retrieve handle to the native actor for POSIX signals}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_signal_actor_handle)
//
// !!! The native scanner isn't smart enough to notice REBNATIVE() inside a
// disabled #ifdef, so a definition for this has to be provided... even if
// it's not a build where it should be available.
{
#ifdef HAS_POSIX_SIGNAL
    Make_Port_Actor_Handle(D_OUT, &Signal_Actor);
    return D_OUT;
#else
    UNUSED(frame_);
    fail ("GET-SIGNAL-ACTOR-HANDLE only works in builds with POSIX signals");
#endif
}
