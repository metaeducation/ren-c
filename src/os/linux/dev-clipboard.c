/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
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
**  Title: Device: Clipboard access for X
**  Author: Shixin Zeng
**  Purpose:
**      Provides a very simple interface to the clipboard for text.
**      May be expanded in the future for images, etc.
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

#include <X11/Xlib.h>

#include "reb-host.h"
#include "host-lib.h"

#include "host-window.h"

extern x_info_t *global_x_info;

/***********************************************************************
**
*/	DEVICE_CMD Open_Clipboard(REBREQ *req)
/*
***********************************************************************/
{
	//RL_Print("Open_Clipboard\n");
	SET_OPEN(req);
	return DR_DONE;
}


/***********************************************************************
**
*/	DEVICE_CMD Close_Clipboard(REBREQ *req)
/*
***********************************************************************/
{
	//RL_Print("Close_Clipboard\n");
	SET_CLOSED(req);
	return DR_DONE;
}

static REBINT copy_to_req(REBREQ *req, char* data, REBCNT data_len)
{
	req->data = OS_Make(data_len + 1);
	if (req->data == NULL){
		return DR_ERROR;
	}
	COPY_STR(req->data, data, data_len);
	req->data[data_len] = '\0';
	req->actual = data_len;
	return DR_DONE;
}

static REBINT do_read_clipboard(REBREQ * req, Atom property)
{
	Atom     actual_type;
	int      actual_format;
	long     nitems;
	long     bytes;
	char     *data = NULL;
	int      status;
	if (global_x_info->selection.property){
		status = XGetWindowProperty(global_x_info->display,
									global_x_info->selection.win,
									property,
									0,
									(~0L),
									False,
									AnyPropertyType,
									&actual_type,
									&actual_format,
									&nitems,
									&bytes,
									(unsigned char**)&data);
		if (nitems <= 0){
			req->actual = 0;
			return DR_ERROR;
		}

		if (DR_ERROR == copy_to_req(req, data, nitems)){;
			XFree(data);
			return DR_ERROR;
		}
		XFree(data);

		Signal_Device(req, EVT_READ);
		//RL_Print("do_read_clipboard succeeded\n");
		return DR_DONE;
	}

	//RL_Print("do_read_clipboard failed\n");
	return DR_ERROR;
}

/***********************************************************************
**
*/	DEVICE_CMD Read_Clipboard(REBREQ *req)
/*
***********************************************************************/
{
	REBYTE *clip = NULL;
	REBINT len;
	REBINT status = global_x_info->selection.status;

	//RL_Print("Read_Clipboard\n");
	req->actual = 0;
	Window owner = 0;
	Display *display = global_x_info->display;
	Atom XA_CLIPBOARD = XInternAtom(display, "CLIPBOARD", 0);
	Atom XA_SELECTION = XInternAtom(global_x_info->display, "REBOL_SELECTION", False);
	//XSync(display, False);
	owner = XGetSelectionOwner(display, XA_CLIPBOARD);
	if (global_x_info->selection.win == owner){
		/* same process, bypass the server */
		if (global_x_info->selection.data != NULL){
			if (DR_ERROR == copy_to_req(req,
										global_x_info->selection.data,
										global_x_info->selection.data_length)){
				return DR_ERROR;
			}
			global_x_info->selection.status = -1;
			return DR_DONE;
		} else {
			return DR_ERROR;
		}
	}
	if (status < 0) { /* request not sent yet */
		if (global_x_info->selection.win == 0){
			global_x_info->selection.win
				= XCreateWindow(display,
								RootWindow(display, 0),
								0, 0, 50, 50, 0,
								CopyFromParent, InputOnly,
							   	CopyFromParent, 0,0);
		}
		//XSync(display, False);
		Atom XA_TARGETS = XInternAtom(global_x_info->display, "TARGETS", False);
		XConvertSelection(display,
						  XA_CLIPBOARD, XA_TARGETS, XA_SELECTION,
						  global_x_info->selection.win,
						  CurrentTime);
		global_x_info->selection.status = 0; /* pending */
		return DR_PEND;
	} else if (status) { /* response received */
		global_x_info->selection.status = -1; /* prep for next read */
		return do_read_clipboard(req, XA_SELECTION);
	} else { /* request sent and response not received yet */
		return DR_PEND;
	}
}


/***********************************************************************
**
*/	DEVICE_CMD Write_Clipboard(REBREQ *req)
/*
**		Works for Unicode and ASCII strings.
**		Length is number of bytes passed (not number of chars).
**
***********************************************************************/
{
	//RL_Print("Write_Clipboard\n");
	//XStoreBytes(global_x_info->display, req->data, len);
	Window win = global_x_info->selection.win;
	Display *display = global_x_info->display;
	Atom XA_CLIPBOARD = XInternAtom(display, "CLIPBOARD", 0);

	if (win == 0){
		win = global_x_info->selection.win
			= XCreateWindow(display,
							RootWindow(display, 0),
							0, 0, 50, 50, 0,
							CopyFromParent, InputOnly,
							CopyFromParent, 0,0);
		//RL_Print("window = %d\n", win);
	}
	void *data = global_x_info->selection.data;
	if (data != NULL) {
		OS_Free(data);
	}
	REBINT src_len = req->length;
	if (GET_FLAG(req->flags, RRF_WIDE)){
		src_len /= sizeof(REBUNI);
	}
	REBCNT len = Length_As_UTF8(req->data, src_len, TRUE, 0);
	data = global_x_info->selection.data = OS_Make(len);
	if (data == NULL) {
		return DR_ERROR;
	}
	REBCNT dst_len = src_len;
	Encode_UTF8(data, len, req->data, &dst_len, TRUE, 0);
	global_x_info->selection.data_length = dst_len;
	//XSync(display, False);
	XSetSelectionOwner(display, XA_CLIPBOARD, win, CurrentTime);
	//XFlush(display);
	req->actual = dst_len;
	Signal_Device(req, EVT_WROTE);
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
	Open_Clipboard,
	Close_Clipboard,
	Read_Clipboard,
	Write_Clipboard,
	0,
};

DEFINE_DEV(Dev_Clipboard, "Clipboard", 1, Dev_Cmds, RDC_MAX, 0);
