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
**  Title: Device: Event handler for Posix
**  Author: Carl Sassenrath
**  Purpose:
**      Processes events to pass to REBOL. Note that events are
**      used for more than just windowing.
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
#include <unistd.h>
#include <sys/time.h>

#include  <X11/Xlib.h>

#include "reb-host.h"
#include "host-lib.h"

extern Display *x_display;
void Done_Device(int handle, int error);

static void Add_Event_XY(REBGOB *gob, REBINT id, REBINT xy, REBINT flags)
{
	REBEVT evt;

	evt.type  = id;
	evt.flags = (u8) (flags | (1<<EVF_HAS_XY));
	evt.model = EVM_GUI;
	evt.data  = xy;
	evt.ser = (void*)gob;

	RL_Event(&evt);	// returns 0 if queue is full
}

static void Add_Event_Key(REBGOB *gob, REBINT id, REBINT key, REBINT flags)
{
	REBEVT evt;

	evt.type  = id;
	evt.flags = flags;
	evt.model = EVM_GUI;
	evt.data  = key;
	evt.ser = (void*)gob;

	RL_Event(&evt);	// returns 0 if queue is full
}
/***********************************************************************
**
*/	DEVICE_CMD Init_Events(REBREQ *dr)
/*
**		Initialize the event device.
**
**		Create a hidden window to handle special events,
**		such as timers and async DNS.
**
***********************************************************************/
{
	REBDEV *dev = (REBDEV*)dr; // just to keep compiler happy
	SET_FLAG(dev->flags, RDF_INIT);
	return DR_DONE;
}


/***********************************************************************
**
*/	DEVICE_CMD Poll_Events(REBREQ *req)
/*
**		Poll for events and process them.
**		Returns 1 if event found, else 0.
**
***********************************************************************/
{
	int flag = DR_DONE;
	XEvent ev;
	REBGOB *gob = NULL;
	// Handle XEvents and flush the input 
	KeySym *keysym = NULL;
    REBINT keysyms_per_keycode_return;
	REBINT xyd = 0;
	while(XPending(x_display)) {
		XNextEvent(x_display, &ev);
		switch (ev.type) {
			case Expose:
				RL_Print("exposed\n");
				break;
			case ButtonPress:
				RL_Print("Button %d pressed\n", ev.xbutton.button);
				xyd = (ROUND_TO_INT(ev.xbutton.x / dp_scale.x)) + (ROUND_TO_INT(ev.xbutton.y / dp_scale.y) << 16);
				Add_Event_XY(gob, EVT_DOWN, xyd, 0);
				break;

			case ButtonRelease:
				RL_Print("Button %d is released\n", ev.xbutton.button);
				xyd = (ROUND_TO_INT(ev.xbutton.x / dp_scale.x)) + (ROUND_TO_INT(ev.xbutton.y / dp_scale.y) << 16);
				Add_Event_XY(gob, EVT_UP, xyd, 0);
				break;

			case MotionNotify:
				RL_Print ("mouse motion\n");
				gob = Find_Gob(ev.xmotion.window);
				xyd = (ROUND_TO_INT(ev.xmotion.x / dp_scale.x)) + (ROUND_TO_INT(ev.xmotion.y / dp_scale.y) << 16);
				Add_Event_XY(gob, EVT_MOVE, xyd, 0);
				break;
			case KeyPress:
				RL_Print ("key %s is pressed\n", XKeysymToString(XKeycodeToKeysym(x_display, ev.xkey.keycode, 0)));
				keysym = XGetKeyboardMapping(x_display,
											 ev.xkey.keycode,
											 1,
											 &keysyms_per_keycode_return);

				gob = Find_Gob(ev.xkey.window);
				if(gob != NULL){
					Add_Event_Key(gob, EVT_KEY, keysym[0], 0);
				}

				XFree(keysym);
				break;
			case KeyRelease:
				RL_Print ("key %s is released\n", XKeysymToString(XKeycodeToKeysym(x_display, ev.xkey.keycode, 0)));
				keysym = XGetKeyboardMapping(x_display,
													 ev.xkey.keycode,
													 1,
													 &keysyms_per_keycode_return);

				gob = Find_Gob(ev.xkey.window);
				if(gob != NULL){
					Add_Event_Key(gob, EVT_KEY_UP, keysym[0], 0);
				}

				XFree(keysym);
				RL_Print ("key %s is released\n", XKeysymToString(XKeycodeToKeysym(x_display, ev.xkey.keycode, 0)));
				break;
			case ResizeRequest:
				RL_Print ("request to resize to %dx%d", ev.xresizerequest.width, ev.xresizerequest.height);
				break;
			case DestroyNotify:
				RL_Print ("destroyed\n");
				break;
			case ClientMessage:
				RL_Print ("closed\n");
				gob = Find_Gob(ev.xclient.window);
				Add_Event_XY(gob, EVT_CLOSE, 0, 0);
				break;
			default:
				RL_Print("default event type\n");
				break;
		}
	}
	return flag;	// different meaning compared to most commands
}


/***********************************************************************
**
*/	DEVICE_CMD Query_Events(REBREQ *req)
/*
**		Wait for an event or a timeout sepecified by req->length.
**		This is used by WAIT as the main timing method.
**
***********************************************************************/
{
	struct timeval tv;
	int result;
	fd_set in_fds;

	tv.tv_sec = 0;
	tv.tv_usec = req->length * 1000;
	//printf("usec %d\n", tv.tv_usec);
	
    // This returns the FD of the X11 display (or something like that)
    int x11_fd = ConnectionNumber(x_display);

        // Create a File Description Set containing x11_fd
	FD_ZERO(&in_fds);
	FD_SET(x11_fd, &in_fds);

	// Wait for X Event or a Timer
	if (select(x11_fd+1, &in_fds, 0, 0, &tv)){
		RL_Print("Event Received!\n");
		return DR_PEND;
	}
	else {
		// Handle timer here
		//RL_Print("Timer Fired!\n");
	}

	return DR_DONE;
}


/***********************************************************************
**
*/	DEVICE_CMD Connect_Events(REBREQ *req)
/*
**		Simply keeps the request pending for polling purposes.
**		Use Abort_Device to remove it.
**
***********************************************************************/
{
	return DR_PEND;	// keep pending
}


/***********************************************************************
**
**	Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] = {
	Init_Events,			// init device driver resources
	0,	// RDC_QUIT,		// cleanup device driver resources
	0,	// RDC_OPEN,		// open device unit (port)
	0,	// RDC_CLOSE,		// close device unit
	0,	// RDC_READ,		// read from unit
	0,	// RDC_WRITE,		// write to unit
	Poll_Events,
	Connect_Events,
	Query_Events,
};

DEFINE_DEV(Dev_Event, "OS Events", 1, Dev_Cmds, RDC_MAX, 0);
