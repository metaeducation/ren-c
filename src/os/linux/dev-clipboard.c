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


/***********************************************************************
**
*/	DEVICE_CMD Read_Clipboard(REBREQ *req)
/*
***********************************************************************/
{
	REBYTE *clip = NULL;
	REBINT len;

	//RL_Print("Read_Clipboard\n");
	req->actual = 0;
	clip = XFetchBytes(global_x_info->display, &req->actual);
	if (clip != NULL){
		req->data = OS_Make((req->actual + 1) * sizeof(REBCHR));
		if (req->data == NULL){
			return DR_ERROR;
		}
		memcpy(req->data, clip, req->actual);
		XFree(clip);
	}
	SET_FLAG(req->flags, RRF_WIDE);
	return DR_DONE;
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
	REBINT len = req->length; // in bytes

	//RL_Print("Write_Clipboard\n");
	XStoreBytes(global_x_info->display, req->data, len);

	req->actual = len;
	return DR_DONE;
}


/***********************************************************************
**
*/	DEVICE_CMD Poll_Clipboard(REBREQ *req)
/*
***********************************************************************/
{
	//RL_Print("Poll_Clipboard\n");
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
	Poll_Clipboard,
};

DEFINE_DEV(Dev_Clipboard, "Clipboard", 1, Dev_Cmds, RDC_MAX, 0);
