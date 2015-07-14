/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  Copyright 2014 Atronix Engineering, Inc.
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
**  Module:  p-signal.c
**  Summary: signal port interface
**  Section: ports
**  Author:  Shixin Zeng
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

#ifdef HAS_POSIX_SIGNAL
#include <sys/signal.h>

static void update(REBREQ *req, REBINT len, REBVAL *arg)
{
	const siginfo_t *sig = cast(siginfo_t *, req->common.data);
	int i = 0;

	Extend_Series(VAL_SERIES(arg), len);

	for (i = 0; i < len; i ++) {
		REBSER *obj = Make_Frame(2);
		REBVAL *val = Append_Frame(obj, NULL, Make_Word(cb_cast("signal-no"), 0));
		SET_INTEGER(val, sig[i].si_signo);

		val = Append_Frame(obj, NULL, Make_Word(cb_cast("code"), 0));
		SET_INTEGER(val, sig[i].si_code);
		val = Append_Frame(obj, NULL, Make_Word(cb_cast("source-pid"), 0));
		SET_INTEGER(val, sig[i].si_pid);
		val = Append_Frame(obj, NULL, Make_Word(cb_cast("source-uid"), 0));
		SET_INTEGER(val, sig[i].si_uid);

		Set_Object(VAL_BLK_SKIP(arg, VAL_TAIL(arg) + i), obj);
	}

	VAL_TAIL(arg) += len;

	req->actual = 0; /* avoid duplicate updates */
}

static int sig_word_num(REBVAL *word)
{
	switch (VAL_WORD_CANON(word)) {
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
		default:
			Trap1_DEAD_END(RE_INVALID_SPEC, word);
	}
}

/***********************************************************************
**
*/	static int Signal_Actor(REBVAL *ds, REBSER *port, REBCNT action)
/*
***********************************************************************/
{
	REBREQ *req;
	REBINT result;
	REBVAL *arg;
	REBINT len;
	REBSER *ser;
	REBVAL *spec;
	REBVAL *val;
	REBVAL *sig;

	Validate_Port(port, action);

	req = Use_Port_State(port, RDI_SIGNAL, sizeof(REBREQ));
	spec = OFV(port, STD_PORT_SPEC);

	if (!IS_OPEN(req)) {
		switch (action) {
			case A_READ:
			case A_OPEN:
				val = Obj_Value(spec, STD_PORT_SPEC_SIGNAL_MASK);
				if (!IS_BLOCK(val)) {
					Trap1_DEAD_END(RE_INVALID_SPEC, val);
				}

				sigemptyset(&req->special.signal.mask);
				for(sig = VAL_BLK_SKIP(val, 0); NOT_END(sig); sig ++) {
					if (IS_WORD(sig)) {
						/* handle the special word "ALL" */
						if (VAL_WORD_CANON(sig) == SYM_ALL) {
							if (sigfillset(&req->special.signal.mask) < 0) {
								Trap1_DEAD_END(RE_INVALID_SPEC, sig); /* FIXME, better error */
							}
							break;
						}

						if (sigaddset(&req->special.signal.mask, sig_word_num(sig)) < 0) {
							Trap1_DEAD_END(RE_INVALID_SPEC, sig);
						}
					} else {
						Trap1_DEAD_END(RE_INVALID_SPEC, sig);
					}
				}

				if (OS_DO_DEVICE(req, RDC_OPEN)) Trap_Port_DEAD_END(RE_CANNOT_OPEN, port, req->error);
				if (action == A_OPEN) {
					return R_ARG1; //port
				}
				break;
			case A_CLOSE:
				return R_RET;

			case A_OPENQ:
				return R_FALSE;

			case A_UPDATE:	// allowed after a close
				break;

			default:
				Trap_Port_DEAD_END(RE_NOT_OPEN, port, -12);
		}
	}

	switch (action) {
		case A_UPDATE:
			// Update the port object after a READ or WRITE operation.
			// This is normally called by the WAKE-UP function.
			arg = OFV(port, STD_PORT_DATA);
			if (req->command == RDC_READ) {
				len = req->actual;
				if (len > 0) {
					update(req, len, arg);
				}
			}
			return R_NONE;

		case A_READ:
			// This device is opened on the READ:
			// Issue the read request:
			arg = OFV(port, STD_PORT_DATA);

			len = req->length = 8;
			ser = Make_Binary(len * sizeof(siginfo_t));
			req->common.data = BIN_HEAD(ser);
			result = OS_DO_DEVICE(req, RDC_READ);
			if (result < 0) Trap_Port_DEAD_END(RE_READ_ERROR, port, req->error);

			arg = OFV(port, STD_PORT_DATA);
			if (!IS_BLOCK(arg)) {
				Set_Block(arg, Make_Block(len));
			}

			len = req->actual;

			if (len > 0) {
				update(req, len, arg);
				*D_RET = *arg;
				return R_RET;
			} else {
				return R_NONE;
			}

		case A_CLOSE:
			OS_DO_DEVICE(req, RDC_CLOSE);
			return R_ARG1;

		case A_OPENQ:
			return R_TRUE;

		case A_OPEN:
			Trap1_DEAD_END(RE_ALREADY_OPEN, D_ARG(1));

		default:
			Trap_Action_DEAD_END(REB_PORT, action);
	}

	return R_RET;
}


/***********************************************************************
**
*/	void Init_Signal_Scheme(void)
/*
***********************************************************************/
{
	Register_Scheme(SYM_SIGNAL, 0, Signal_Actor);
}

#endif //HAS_POSIX_SIGNAL
