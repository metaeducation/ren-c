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

#include "reb-host.h"

extern void Signal_Device(REBREQ *req, REBINT type);

/***********************************************************************
**
*/	DEVICE_CMD Open_Signal(REBREQ *req)
/*
***********************************************************************/
{
	//RL_Print("Open_Signal\n");

	sigset_t mask;
	sigset_t overlap;

#ifdef CHECK_MASK_OVERLAP //doesn't work yet
	if (sigprocmask(SIG_BLOCK, NULL, &mask) < 0) {
		goto error;
	}
	if (sigandset(&overlap, &mask, &req->special.signal.mask) < 0) {
		goto error;
	}
	if (!sigisemptyset(&overlap)) {
		req->error = EBUSY;
		return DR_ERROR;
	}
#endif

	if (sigprocmask(SIG_BLOCK, &req->special.signal.mask, NULL) < 0) {
		goto error;
	}

	SET_OPEN(req);
	Signal_Device(req, EVT_OPEN);

	return DR_DONE;

error:
	req->error = errno;
	return DR_ERROR;
}

/***********************************************************************
**
*/	DEVICE_CMD Close_Signal(REBREQ *req)
/*
***********************************************************************/
{
	//RL_Print("Close_Signal\n");
	if (sigprocmask(SIG_UNBLOCK, &req->special.signal.mask, NULL) < 0) {
		goto error;
	}
	SET_CLOSED(req);
	return DR_DONE;

error:
	req->error = errno;
	return DR_ERROR;
}

/***********************************************************************
*/	DEVICE_CMD Read_Signal(REBREQ *req)
/*
***********************************************************************/
{
	struct timespec timeout = {0, 0};
	unsigned int i = 0;

	errno = 0;

	for (i = 0; i < req->length; i ++) {
		int result = sigtimedwait(
			&req->special.signal.mask,
			&(cast(siginfo_t*, req->common.data)[i]),
			&timeout
		);

		if (result < 0) {
			if (errno != EAGAIN && i == 0) {
				Signal_Device(req, EVT_ERROR);
				return DR_ERROR;
			} else {
				break;
			}
		}
	}

	req->actual = i;
	if (i > 0) {
	//printf("read %d signals\n", req->actual);
		Signal_Device(req, EVT_READ);
		return DR_DONE;
	} else {
		return DR_PEND;
	}
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
