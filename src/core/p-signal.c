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
#include <sys/signalfd.h>

static void update(REBREQ *req, REBINT len, REBVAL *arg)
{
	const struct signalfd_siginfo *sig = req->data;
	int i = 0;

	Extend_Series(VAL_SERIES(arg), len);

	for (i = 0; i < len; i ++) {
		REBSER *obj = Make_Frame(2);
		REBVAL *val = Append_Frame(obj, NULL, Make_Word("signal-no", 0));
		SET_INTEGER(val, sig[i].ssi_signo);

		val = Append_Frame(obj, NULL, Make_Word("source-pid", 0));
		SET_INTEGER(val, sig[i].ssi_pid);

		Set_Object(VAL_BLK_SKIP(arg, VAL_TAIL(arg) + i), obj);
	}

	VAL_TAIL(arg) += len;

	req->actual = 0; /* avoid duplicate updates */
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
	REBCNT refs;	// refinement argument flags
	REBINT len;
	REBSER *ser;

	Validate_Port(port, action);

	arg = D_ARG(2);

	req = Use_Port_State(port, RDI_SIGNAL, sizeof(REBREQ));

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
		if (!IS_OPEN(req)) {
			if (OS_DO_DEVICE(req, RDC_OPEN)) Trap_Port(RE_CANNOT_OPEN, port, req->error);
		}
		// Issue the read request:
		arg = OFV(port, STD_PORT_DATA);

		len = req->length = 8;
		ser = Make_Binary(len * sizeof(struct signalfd_siginfo));
		req->data = BIN_HEAD(ser);
		result = OS_DO_DEVICE(req, RDC_READ);
		if (result < 0) Trap_Port(RE_READ_ERROR, port, req->error);

		// Copy and set the string result:
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

	case A_OPEN:
		if (OS_DO_DEVICE(req, RDC_OPEN)) Trap_Port(RE_CANNOT_OPEN, port, req->error);
		break;

	case A_CLOSE:
		OS_DO_DEVICE(req, RDC_CLOSE);
		break;

	case A_OPENQ:
		if (IS_OPEN(req)) return R_TRUE;
		return R_FALSE;

	default:
		Trap_Action(REB_PORT, action);
	}

	return R_ARG1; // port
}


/***********************************************************************
**
*/	void Init_Signal_Scheme(void)
/*
***********************************************************************/
{
	Register_Scheme(SYM_SIGNAL, 0, Signal_Actor);
}
