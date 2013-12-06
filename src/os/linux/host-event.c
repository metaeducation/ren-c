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
**  Title: Device: Event handler for X window
**  Author: Shixin Zeng
**  Purpose:
**      Processes X events to pass to REBOL
*/

#include <math.h>
#include  <X11/Xlib.h>

#include "reb-host.h"

#include "host-window.h"
#include "host-compositor.h"

extern x_info_t *global_x_info;
REBGOB *Find_Gob_By_Window(Window win);
extern void* Find_Compositor(REBGOB *gob);

#define GOB_COMPOSITOR(gob)	(Find_Compositor(gob)) //gets handle to window's compositor

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

static void Update_Event_XY(REBGOB *gob, REBINT id, REBINT xy, REBINT flags)
{
	REBEVT evt;

	evt.type  = id;
	evt.flags = (u8) (flags | (1<<EVF_HAS_XY));
	evt.model = EVM_GUI;
	evt.data  = xy;
	evt.ser = (void*)gob;

	RL_Update_Event(&evt);
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

void Dispatch_Events()
{
	XEvent ev;
	REBGOB *gob = NULL;
	// Handle XEvents and flush the input 
	KeySym *keysym = NULL;
    REBINT keysyms_per_keycode_return;
	REBINT xyd = 0;
	XConfigureEvent xce;
	while(XPending(global_x_info->display)) {
		XNextEvent(global_x_info->display, &ev);
		switch (ev.type) {
			case Expose:
				RL_Print("exposed\n");
				gob = Find_Gob_By_Window(ev.xexpose.window);
				if (gob != NULL){
					rebcmp_blit(GOB_COMPOSITOR(gob));
				}
				break;
			case ButtonPress:
				RL_Print("Button %d pressed\n", ev.xbutton.button);
				gob = Find_Gob_By_Window(ev.xbutton.window);
				if (gob != NULL){
					xyd = (ROUND_TO_INT(PHYS_COORD_X(ev.xbutton.x))) + (ROUND_TO_INT(PHYS_COORD_Y(ev.xbutton.y)) << 16);
					Add_Event_XY(gob, EVT_DOWN, xyd, 0);
				}
				break;

			case ButtonRelease:
				RL_Print("Button %d is released\n", ev.xbutton.button);
				gob = Find_Gob_By_Window(ev.xbutton.window);
				if (gob != NULL){
					xyd = (ROUND_TO_INT(PHYS_COORD_X(ev.xbutton.x))) + (ROUND_TO_INT(PHYS_COORD_Y(ev.xbutton.y)) << 16);
					Add_Event_XY(gob, EVT_UP, xyd, 0);
				}
				break;

			case MotionNotify:
				//RL_Print ("mouse motion\n");
				gob = Find_Gob_By_Window(ev.xmotion.window);
				if (gob != NULL){
					xyd = (ROUND_TO_INT(PHYS_COORD_X(ev.xmotion.x))) + (ROUND_TO_INT(PHYS_COORD_Y(ev.xmotion.y)) << 16);
					Update_Event_XY(gob, EVT_MOVE, xyd, 0);
				}
				break;
			case KeyPress:
				//RL_Print ("key %s is pressed\n", XKeysymToString(XKeycodeToKeysym(global_x_info->display, ev.xkey.keycode, 0)));
				keysym = XGetKeyboardMapping(global_x_info->display,
											 ev.xkey.keycode,
											 1,
											 &keysyms_per_keycode_return);

				gob = Find_Gob_By_Window(ev.xkey.window);
				if(gob != NULL){
					Add_Event_Key(gob, EVT_KEY, keysym[0], 0);
				}

				XFree(keysym);
				break;
			case KeyRelease:
				//RL_Print ("key %s is released\n", XKeysymToString(XKeycodeToKeysym(global_x_info->display, ev.xkey.keycode, 0)));
				keysym = XGetKeyboardMapping(global_x_info->display,
													 ev.xkey.keycode,
													 1,
													 &keysyms_per_keycode_return);

				gob = Find_Gob_By_Window(ev.xkey.window);
				if(gob != NULL){
					Add_Event_Key(gob, EVT_KEY_UP, keysym[0], 0);
				}

				XFree(keysym);
				//RL_Print ("key %s is released\n", XKeysymToString(XKeycodeToKeysym(global_x_info->display, ev.xkey.keycode, 0)));
				break;
			case ResizeRequest:
				RL_Print ("request to resize to %dx%d", ev.xresizerequest.width, ev.xresizerequest.height);
				break;
			case FocusIn:
				if (ev.xfocus.mode != NotifyWhileGrabbed) {
					RL_Print ("FocusIn, type = %d, window = %x\n", ev.xfocus.type, ev.xfocus.window);
					gob = Find_Gob_By_Window(ev.xfocus.window);
					if (gob && !GET_GOB_STATE(gob, GOBS_ACTIVE)) {
						SET_GOB_STATE(gob, GOBS_ACTIVE);
						Add_Event_XY(gob, EVT_ACTIVE, 0, 0);
					}
				}
				break;
			case FocusOut:
				if (ev.xfocus.mode != NotifyWhileGrabbed) {
					RL_Print ("FocusOut, type = %d, window = %x\n", ev.xfocus.type, ev.xfocus.window);
					gob = Find_Gob_By_Window(ev.xfocus.window);
					if (gob && GET_GOB_STATE(gob, GOBS_ACTIVE)) {
						CLR_GOB_STATE(gob, GOBS_ACTIVE);
						Add_Event_XY(gob, EVT_INACTIVE, 0, 0);
					}
				}
				break;
			case DestroyNotify:
				RL_Print ("destroyed %x\n", ev.xdestroywindow.window);
				gob = Find_Gob_By_Window(ev.xdestroywindow.window);
				if (gob != NULL){
					Free_Window(gob);
				}
				break;
			case ClientMessage:
				RL_Print ("closed\n");
				gob = Find_Gob_By_Window(ev.xclient.window);
				if (gob != NULL){
					Add_Event_XY(gob, EVT_CLOSE, 0, 0);
				}
				break;
			case ConfigureNotify:
				RL_Print("configuranotify\n");
				xce = ev.xconfigure;
				gob = Find_Gob_By_Window(ev.xconfigure.window);
				if (gob != NULL) {
					if (gob->offset.x != xce.x || gob->offset.y != xce.y){
						xyd = (ROUND_TO_INT(xce.x)) + (ROUND_TO_INT(xce.y) << 16);
						//RL_Print("%s, %s, %d: EVT_OFFSET is sent\n", __FILE__, __func__, __LINE__);
						Update_Event_XY(gob, EVT_OFFSET, xyd, 0);
					}
					xyd = (ROUND_TO_INT(xce.x)) + (ROUND_TO_INT(xce.y) << 16);
					gob->offset.x = xce.x;
					gob->offset.y = xce.y;
					RL_Print("WM_MOVE: %x\n", xyd);
					if (gob->size.x != xce.width || gob->size.y != xce.height){
						Resize_Window(gob, TRUE);
						xyd = (ROUND_TO_INT(xce.width)) + (ROUND_TO_INT(xce.height) << 16);
						RL_Print("%s, %s, %d: EVT_RESIZE is sent\n", __FILE__, __func__, __LINE__);
						Update_Event_XY(gob, EVT_RESIZE, xyd, 0);
					}
					gob->size.x = xce.width;
					gob->size.y = xce.height;
				}
				break;
			default:
				RL_Print("default event type\n");
				break;
		}
	}
}
