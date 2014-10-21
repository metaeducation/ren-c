/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  Copyright 2014 Atronix Engineering, Inc.
**  REBOL is a trademark of REBOL Technologies
**
**  Additional code modifications and improvements Copyright 2012 Saphirion AG
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
**  Title: Device: Signal access on Linux
**  Author: Shixin Zeng
**  Purpose:
**      Provides a very simple interface to the signals on Linux
**
************************************************************************
**
**  NOTE to PROGRAMMERS:
**
**    1. Keep code clear and simple.
**    2. Document unusual code, reasoning, or gotchas.
**    3. Use same style for code, vars, indent(4), comments, etc.
**    4. Keep in mind Linux, OS X, BSD, big/little endian CPUs.
**    5. Test everything, then test it again.
**
***********************************************************************/
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/signal.h>
#include <sys/signalfd.h>

#include "reb-host.h"
#include "host-lib.h"

extern void Signal_Device(REBREQ *req, REBINT type);

static sigset_t omask; /* old signal mask */
static REBOOL already_open = FALSE; /* signal port can only be open once */

/***********************************************************************
**
*/	DEVICE_CMD Open_Signal(REBREQ *req)
/*
***********************************************************************/
{
	//RL_Print("Open_Signal\n");
	sigset_t mask;

	if (already_open && req->socket == 0) {
		req->error = EBUSY;
		return DR_ERROR;
	}

	sigfillset(&mask);

	/* old mask is only needed to be restored if signalfd is called for the first time */
	sigprocmask(SIG_BLOCK, &mask, req->socket > 0 ? NULL : &omask);

	req->signal.restore_omask = (req->socket <= 0);

	req->socket = signalfd(req->socket > 0? req->socket : -1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
	if (req->socket < 0) {
		req->error = errno;
		return DR_ERROR;
	}
	SET_OPEN(req);

	already_open = TRUE;
	return DR_DONE;
}

/***********************************************************************
**
*/	DEVICE_CMD Close_Signal(REBREQ *req)
/*
***********************************************************************/
{
	//RL_Print("Close_Signal\n");
	close(req->socket);
	if (req->signal.restore_omask) {
		sigset_t cmask;
		sigprocmask(SIG_SETMASK, &omask, NULL); /* restore signal mask */
	}
	SET_CLOSED(req);
	if (already_open && req->signal.restore_omask) {
		already_open = FALSE;
	}
	return DR_DONE;
}

/***********************************************************************
*/	DEVICE_CMD Read_Signal(REBREQ *req)
/*
***********************************************************************/
{
	errno = 0;
	ssize_t nbytes = read(req->socket, req->data,
						  req->length * sizeof(struct signalfd_siginfo));
	if (nbytes < 0) {
		//perror("read signal failed");
		if (errno != EAGAIN) {
			Signal_Device(req, EVT_ERROR);
			return DR_ERROR;
		}
		return DR_PEND;
	}

	req->actual = nbytes / sizeof(struct signalfd_siginfo);
	//printf("read %d signals\n", req->actual);
	Signal_Device(req, EVT_READ);
	return DR_DONE;
}


/***********************************************************************
**
**	Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] =
{
	0,
	0,
	Open_Signal,
	Close_Signal,
	Read_Signal,
	0,
	0,
};

DEFINE_DEV(Dev_Signal, "Signal", 1, Dev_Cmds, RDC_MAX, 0);
