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
**  Title: <platform> Windowing support
**  Author: Richard Smolak
**  File:  host-window.c
**  Purpose: Provides functions for windowing.
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

#include <string.h>
#include <math.h>
#include <unistd.h>
#include "reb-host.h"
#include "host-compositor.h"

#include "host-lib.h"
#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>

#include "host-window.h"

//***** Constants *****

#define GOB_HWIN(gob)	((Window)Find_Window(gob))
#define GOB_COMPOSITOR(gob)	(Find_Compositor(gob)) //gets handle to window's compositor

//***** Externs *****
extern REBGOBWINDOWS *Gob_Windows;
extern void Free_Window(REBGOB *gob);
extern void* Find_Compositor(REBGOB *gob);
extern REBINT Alloc_Window(REBGOB *gob);
extern void Draw_Window(REBGOB *wingob, REBGOB *gob);
extern void Dispatch_Events();

x_info_t *global_x_info = NULL;
//***** Locals *****

#define MAX_WINDOWS 64 //must be in sync with os/host-view.c

REBGOB *Find_Gob_By_Window(Window win)
{
	int i = 0;
	for(i = 0; i < MAX_WINDOWS; i ++ ){
		if (Gob_Windows[i].win == (void*)win){
			return Gob_Windows[i].gob;
		}
	}
	return NULL;
}

static REBXYF Zero_Pair = {0, 0};
//**********************************************************************
//** OSAL Library Functions ********************************************
//**********************************************************************

/***********************************************************************
**
*/	void OS_Init_Windows()
/*
**		Initialize special variables of the graphics subsystem.
**
***********************************************************************/
{
	int depth;
	int red_mask, green_mask, blue_mask;
	global_x_info = OS_Make(sizeof(x_info_t));
	global_x_info->display = XOpenDisplay(NULL);
	if (global_x_info->display == NULL){
		RL_Print("XOpenDisplay failed");
	}else{
		RL_Print("XOpenDisplay succeeded: x_dislay = %x\n", global_x_info->display);
	}

	global_x_info->default_screen = DefaultScreenOfDisplay(global_x_info->display);
	global_x_info->default_visual = DefaultVisualOfScreen(global_x_info->default_screen);
	depth = DefaultDepthOfScreen(global_x_info->default_screen);

	red_mask = global_x_info->default_visual->red_mask;
	green_mask = global_x_info->default_visual->green_mask;
	blue_mask = global_x_info->default_visual->blue_mask;
	if (depth < 15 || red_mask == 0 || green_mask == 0 || blue_mask == 0){
		XCloseDisplay(global_x_info->display);
		Host_Crash("Not supported X window system");
	}
	global_x_info->default_depth = depth;
	global_x_info->sys_pixmap_format = pix_format_undefined;
	switch (global_x_info->default_depth){
		case 15:
			global_x_info->bpp = 16;
			if(red_mask = 0x7C00 && green_mask == 0x3E0 && blue_mask == 0x1F)
				global_x_info->sys_pixmap_format = pix_format_bgr555;
			break;
		case 16:
			global_x_info->bpp = 16;
			if(red_mask = 0xF800 && green_mask == 0x7E0 && blue_mask == 0x1F)
				global_x_info->sys_pixmap_format = pix_format_bgr565;
			break;
		case 24:
		case 32:
			global_x_info->bpp = 32;
			if (red_mask = 0xFF0000 && green_mask == 0xFF00 && blue_mask == 0xFF)
				global_x_info->sys_pixmap_format = pix_format_bgra32;
			else if (blue_mask = 0xFF0000 && green_mask == 0xFF00 && red_mask == 0xFF)
				global_x_info->sys_pixmap_format = pix_format_rgba32;
			break;
		defaut:
			break;
	}

	if (global_x_info->sys_pixmap_format == pix_format_undefined) {
		Host_Crash("System Pixmap format couldn't be determined");
	}
}

/***********************************************************************
**
*/	void OS_Update_Window(REBGOB *gob)
/*
**		Update window parameters.
**
***********************************************************************/
{
	RL_Print("updating window:");
	REBINT x = GOB_LOG_X_INT(gob);
	REBINT y = GOB_LOG_Y_INT(gob);
	REBINT w = GOB_LOG_W_INT(gob);
	REBINT h = GOB_LOG_H_INT(gob);
	RL_Print("x: %d, y: %d, width: %d, height: %d\n", x, y, w, h);
	Window win = GOB_HWIN(gob);
	if (!win) {
		return;
	}
	Resize_Window(gob, FALSE);
	if (x != GOB_XO_INT(gob) || y != GOB_YO_INT(gob)){
		RL_Print("Moving window: %x\n", win);
		XMoveWindow(global_x_info->display, win, x, y);
		if (x + w < 0 
			|| y + h < 0
			|| x > OS_Get_Metrics(SM_SCREEN_WIDTH)
			|| y > OS_Get_Metrics(SM_SCREEN_HEIGHT)) {
			RL_Print("Hiding window: %x\n", win);
			XUnmapWindow(global_x_info->display, win); //hide the out-of-bound window
		} else {
			RL_Print("Unhiding window: %x\n", win);
			XMapWindow(global_x_info->display, win); //unhide the window
		}
	}
}

/***********************************************************************
**
*/  void* OS_Open_Window(REBGOB *gob)
/*
**		Initialize the graphics window.
**
**		The window handle is returned, but not expected to be used
**		other than for debugging conditions.
**
***********************************************************************/
{
	REBINT windex;
	REBINT x = GOB_LOG_X_INT(gob);
	REBINT y = GOB_LOG_Y_INT(gob);
	REBINT w = GOB_LOG_W_INT(gob);
	REBINT h = GOB_LOG_H_INT(gob);

	REBCHR *title;
	REBYTE os_string = FALSE;

	Window window;
	u32 mask = 0;
	u32 values[6];
	//xcb_drawable_t d;
	
	Display *display = global_x_info->display;
	XSetWindowAttributes swa;

	Window parent_window;

	RL_Print("%s, %d, x: %d, y: %d, width: %d, height: %d\n", __func__, __LINE__, x, y, w, h);

	swa.event_mask = ExposureMask 
					| PointerMotionMask 
					| KeyPressMask 
					| KeyReleaseMask
					| ButtonPressMask 
					| ButtonReleaseMask 
					| StructureNotifyMask 
					| FocusChangeMask;

	Atom window_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", True);
	Atom window_type;
	parent_window = DefaultRootWindow(display);
	if (GET_FLAGS(gob->flags, GOBF_NO_TITLE, GOBF_NO_BORDER)) {
		window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", True);
		if (GOB_HWIN(GOB_TMP_OWNER(gob))) {
			parent_window = GOB_HWIN(GOB_TMP_OWNER(gob));
			int parent_x, parent_y, parent_w, parent_h, parent_border_width, parent_depth;
			Window root, grand_window, *children = NULL;
			int n_children = 0;

			XQueryTree(display, parent_window, &root, &grand_window, &children, &n_children);
			if (children){
				XFree(children);
			}

			XGetGeometry(display, parent_window, &root, &parent_x, &parent_y, 
						&parent_w, &parent_h, &parent_border_width, &parent_depth);
 
			int abs_x, abs_y;
			Window child;
			XTranslateCoordinates(display, grand_window, root, parent_x, parent_y, &abs_x, &abs_y, &child);
			x -= abs_x;
	  		y -= abs_y;	   
			RL_Print("%s, %d, x: %d, y: %d, parent_x: %d, parent_y: %d, abs_x: %d, abs_y: %d, width: %d, height: %d\n", __func__, __LINE__, 
					 x, y, parent_x, parent_y, abs_x, abs_y, w, h);
		}
		window = XCreateWindow(display, 
							   parent_window,
							   x, y, w, h,
							   REB_WINDOW_BORDER_WIDTH,
							   CopyFromParent, InputOutput,
							   CopyFromParent, CWEventMask,
							   &swa);
		XSetTransientForHint(display, window, parent_window);
	} else {
		window = XCreateWindow(display, 
							   parent_window,
							   x, y, w, h,
							   REB_WINDOW_BORDER_WIDTH,
							   CopyFromParent, InputOutput,
							   CopyFromParent, CWEventMask,
							   &swa);
		if (GET_GOB_FLAG(gob, GOBF_MODAL)) {
			Atom wm_state = XInternAtom(display, "_NET_WM_STATE", True);
			Atom wm_state_modal = XInternAtom(display, "_NET_WM_STATE_MODAL", True);
			parent_window = GOB_HWIN(GOB_TMP_OWNER(gob));
			XSetTransientForHint(display, window, parent_window);
			int status = XChangeProperty(display, window, wm_state, XA_ATOM, 32,
										 PropModeReplace, (unsigned char*)&wm_state_modal, 1);
			window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", True);
		} else {
			window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_NORMAL", True);
		}
	}
	XChangeProperty(display, window, window_type_atom, XA_ATOM, 32,
					PropModeReplace,
					(unsigned char *)&window_type, 1); 

	if (IS_GOB_STRING(gob))
        os_string = As_OS_Str(GOB_CONTENT(gob), (REBCHR**)&title);
    else
        title = TXT("REBOL Window");

	XTextProperty title_prop;
	Atom title_atom = XInternAtom(display, "_NET_WM_NAME", False);
	XmbTextListToTextProperty(display, (char **)&title, 1, XUTF8StringStyle, &title_prop);
	XSetTextProperty(display, window, &title_prop, title_atom);
	XStoreName(display, window, title); //backup for non NET Wms

	XClassHint *class_hints = XAllocClassHint();
	if (class_hints) {
		class_hints->res_name = title;
		class_hints->res_class = title;
		XSetClassHint(display, window, class_hints);
		XFree(class_hints);
	}

	if (os_string)
		OS_Free(title);

	Atom wmDelete=XInternAtom(display, "WM_DELETE_WINDOW", 1);
	XSetWMProtocols(display, window, &wmDelete, 1);

	XSizeHints *size_hints = XAllocSizeHints();
	if (size_hints) {
		size_hints->flags = PPosition | PSize | PMinSize;
		size_hints->min_width = w;
		size_hints->min_height = h;
	}
	Atom wm_allowed_action = XInternAtom(display, "_NET_WM_ALLOWED_ACTIONS", True);
	if (GET_GOB_FLAG(gob, GOBF_RESIZE)) {
		//RL_Print("Resizable\n");
		Atom wm_actions[] = {
			XInternAtom(display, "_NET_WM_ACTION_MOVE", True),
			XInternAtom(display, "_NET_WM_ACTION_RESIZE", True),
			XInternAtom(display, "_NET_WM_ACTION_CLOSE", True)
		};
		XChangeProperty(display, window, wm_allowed_action, XA_ATOM, 32,
						PropModeReplace, (unsigned char*)&wm_actions[0],
						sizeof(wm_actions)/sizeof(wm_actions[0]));
	} else {
		//RL_Print("Non-Resizable\n");
		Atom wm_actions[] = {
			XInternAtom(display, "_NET_WM_ACTION_MOVE", True),
			XInternAtom(display, "_NET_WM_ACTION_CLOSE", True)
		};
		XChangeProperty(display, window, wm_allowed_action, XA_ATOM, 32,
						PropModeReplace, (unsigned char*)&wm_actions[0],
						sizeof(wm_actions)/sizeof(wm_actions[0])); /* FIXME, this didn't work */

		if (size_hints) {
			//RL_Print("Setting normal size hints %dx%d\n", w, h);
			size_hints->max_width = w;
			size_hints->max_height = h;
			size_hints->flags |= PMaxSize;
		}
	}
	if (size_hints){
		XSetWMNormalHints(display, window, size_hints);
		XFree(size_hints);
	}

	windex = Alloc_Window(gob);

	if (windex < 0) Host_Crash("Too many windows");

	Gob_Windows[windex].win = (void*)window;
	Gob_Windows[windex].compositor = rebcmp_create(Gob_Root, gob);

	if ((x + w > 0 && x < OS_Get_Metrics(SM_SCREEN_WIDTH))
		&& (y + h > 0 && y < OS_Get_Metrics(SM_SCREEN_HEIGHT))) {
		RL_Print("Mapping %x\n", window);
		XMapWindow(display, window);
	}

	int actual_x, actual_y, actual_w, actual_h, actual_border_width, actual_depth;
	Window root;
	XGetGeometry(display, window, &root, &actual_x, &actual_y, 
				 &actual_w, &actual_h, &actual_border_width, &actual_depth);
	RL_Print("%s %d, created an X window: %x for gob %x, x: %d, y: %d, w: %d, h: %d, border: %d, depth: %d\n", 
			 __func__, __LINE__, window, gob,
			 actual_x, actual_y, actual_w, actual_h,
			 actual_border_width, actual_depth);

	CLEAR_GOB_STATE(gob);
	SET_GOB_STATE(gob, GOBS_NEW);

	SET_GOB_FLAG(gob, GOBF_WINDOW);
	SET_GOB_FLAG(gob, GOBF_ACTIVE);	
	SET_GOB_STATE(gob, GOBS_OPEN);

	return (void*)window;
}

/***********************************************************************
**
*/  void OS_Close_Window(REBGOB *gob)
/*
**		Close the window.
**
***********************************************************************/
{
	RL_Print("Closing %x\n", gob);
	if (GET_GOB_FLAG(gob, GOBF_WINDOW)) {
		XSync(global_x_info->display, FALSE); //wait child window to be destroyed and notified
		Dispatch_Events();
		Window win = GOB_HWIN(gob);
		if (win) {
			//RL_Print("Destroying window: %x\n", win);
			XDestroyWindow(global_x_info->display, win);
			Dispatch_Events();

			Free_Window(gob);
		}
	}
}
