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
**  Module:  p-clipboard.c
**  Summary: clipboard port interface
**  Section: ports
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


/***********************************************************************
**
*/	static REB_R Clipboard_Actor(struct Reb_Call *call_, REBSER *port, REBCNT action)
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

	arg = DS_ARGC > 1 ? D_ARG(2) : NULL;

	req = cast(REBREQ*, Use_Port_State(port, RDI_CLIPBOARD, sizeof(REBREQ)));

	switch (action) {
	case A_UPDATE:
		// Update the port object after a READ or WRITE operation.
		// This is normally called by the WAKE-UP function.
		arg = OFV(port, STD_PORT_DATA);
		if (req->command == RDC_READ) {
			// this could be executed twice:
			// once for an event READ, once for the CLOSE following the READ
			if (!req->common.data) return R_NONE;
			len = req->actual;
			if (GET_FLAG(req->flags, RRF_WIDE)) {
				// convert to UTF8, so that it can be converted back to string!
				Val_Init_Binary(arg, Make_UTF8_Binary(
					req->common.data,
					len / sizeof(REBUNI),
					0,
					OPT_ENC_UNISRC
				));
			}
			else {
				REBSER *ser = Make_Binary(len);
				memcpy(BIN_HEAD(ser), req->common.data, len);
				SERIES_TAIL(ser) = len;
				Val_Init_Binary(arg, ser);
			}
			OS_FREE(req->common.data); // release the copy buffer
			req->common.data = 0;
		}
		else if (req->command == RDC_WRITE) {
			SET_NONE(arg);  // Write is done.
		}
		return R_NONE;

	case A_READ:
		// This device is opened on the READ:
		if (!IS_OPEN(req)) {
			if (OS_DO_DEVICE(req, RDC_OPEN))
				raise Error_On_Port(RE_CANNOT_OPEN, port, req->error);
		}
		// Issue the read request:
		CLR_FLAG(req->flags, RRF_WIDE); // allow byte or wide chars
		result = OS_DO_DEVICE(req, RDC_READ);
		if (result < 0) raise Error_On_Port(RE_READ_ERROR, port, req->error);
		if (result > 0) return R_NONE; /* pending */

		// Copy and set the string result:
		arg = OFV(port, STD_PORT_DATA);

		len = req->actual;
		if (GET_FLAG(req->flags, RRF_WIDE)) {
			// convert to UTF8, so that it can be converted back to string!
			Val_Init_Binary(arg, Make_UTF8_Binary(
				req->common.data,
				len / sizeof(REBUNI),
				0,
				OPT_ENC_UNISRC
			));
		}
		else {
			REBSER *ser = Make_Binary(len);
			memcpy(BIN_HEAD(ser), req->common.data, len);
			SERIES_TAIL(ser) = len;
			Val_Init_Binary(arg, ser);
		}

		*D_OUT = *arg;
		return R_OUT;

	case A_WRITE:
		if (!IS_STRING(arg) && !IS_BINARY(arg))
			raise Error_1(RE_INVALID_PORT_ARG, arg);
		// This device is opened on the WRITE:
		if (!IS_OPEN(req)) {
			if (OS_DO_DEVICE(req, RDC_OPEN))
				raise Error_On_Port(RE_CANNOT_OPEN, port, req->error);
		}

		refs = Find_Refines(call_, ALL_WRITE_REFS);

		// Handle /part refinement:
		len = VAL_LEN(arg);
		if (refs & AM_WRITE_PART && VAL_INT32(D_ARG(ARG_WRITE_LIMIT)) < len)
			len = VAL_INT32(D_ARG(ARG_WRITE_LIMIT));

		// If bytes, see if we can fit it:
		if (SERIES_WIDE(VAL_SERIES(arg)) == 1) {
#ifdef ARG_STRINGS_ALLOWED
			if (!All_Bytes_ASCII(VAL_BIN_DATA(arg), len)) {
				Val_Init_String(
					arg, Copy_Bytes_To_Unicode(VAL_BIN_DATA(arg), len)
				);
			} else
				req->common.data = VAL_BIN_DATA(arg);
#endif

			// Temp conversion:!!!
			ser = Make_Unicode(len);
			len = Decode_UTF8(UNI_HEAD(ser), VAL_BIN_DATA(arg), len, FALSE);
			SERIES_TAIL(ser) = len = abs(len);
			UNI_TERM(ser);
			Val_Init_String(arg, ser);
			req->common.data = cast(REBYTE*, UNI_HEAD(ser));
			SET_FLAG(req->flags, RRF_WIDE);
		}
		else
		// If unicode (may be from above conversion), handle it:
		if (SERIES_WIDE(VAL_SERIES(arg)) == sizeof(REBUNI)) {
			req->common.data = cast(REBYTE *, VAL_UNI_DATA(arg));
			SET_FLAG(req->flags, RRF_WIDE);
		}

		// Temp!!!
		req->length = len * sizeof(REBUNI);

		// Setup the write:
		*OFV(port, STD_PORT_DATA) = *arg;	// keep it GC safe
		req->actual = 0;

		result = OS_DO_DEVICE(req, RDC_WRITE);
		SET_NONE(OFV(port, STD_PORT_DATA)); // GC can collect it

		if (result < 0) raise Error_On_Port(RE_WRITE_ERROR, port, req->error);
		//if (result == DR_DONE) SET_NONE(OFV(port, STD_PORT_DATA));
		break;

	case A_OPEN:
		if (OS_DO_DEVICE(req, RDC_OPEN))
			raise Error_On_Port(RE_CANNOT_OPEN, port, req->error);
		break;

	case A_CLOSE:
		OS_DO_DEVICE(req, RDC_CLOSE);
		break;

	case A_OPENQ:
		if (IS_OPEN(req)) return R_TRUE;
		return R_FALSE;

	default:
		raise Error_Illegal_Action(REB_PORT, action);
	}

	return R_ARG1; // port
}


/***********************************************************************
**
*/	void Init_Clipboard_Scheme(void)
/*
***********************************************************************/
{
	Register_Scheme(SYM_CLIPBOARD, 0, Clipboard_Actor);
}
