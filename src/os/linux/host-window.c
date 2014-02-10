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

#include <assert.h>
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

void* Find_Window(REBGOB *gob);
#define GOB_HWIN(gob)	((host_window_t*)Find_Window(gob))
#define GOB_COMPOSITOR(gob)	(Find_Compositor(gob)) //gets handle to window's compositor

//***** Externs *****
extern REBGOBWINDOWS *Gob_Windows;
extern void Free_Window(REBGOB *gob);
extern void* Find_Compositor(REBGOB *gob);
extern REBINT Alloc_Window(REBGOB *gob);
extern void Draw_Window(REBGOB *wingob, REBGOB *gob);
extern void X_Event_Loop();

x_info_t *global_x_info = NULL;
//***** Locals *****

#define MAX_WINDOWS 64 //must be in sync with os/host-view.c
#ifndef HOST_NAME_MAX
  #define HOST_NAME_MAX 256
#endif

REBGOB *Find_Gob_By_Window(Window win)
{
	int i = 0;
	for(i = 0; i < MAX_WINDOWS; i ++ ){
		host_window_t *hw = Gob_Windows[i].win;
		if (hw != NULL && hw->x_id == win) {
			return Gob_Windows[i].gob;
		}
	}
	return NULL;
}

host_window_t *Find_Host_Window_By_ID(Window win)
{
	int i = 0;
	for(i = 0; i < MAX_WINDOWS; i ++ ){
		host_window_t *hw = Gob_Windows[i].win;
		if (hw != NULL && hw->x_id == win) {
			return (host_window_t*)Gob_Windows[i].win;
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

	/* initialize selection */
	global_x_info->selection.win = 0;
	global_x_info->selection.status = -1;
	global_x_info->selection.data = NULL;
	global_x_info->selection.data_length = 0;
	global_x_info->leader_window = 0;
	global_x_info->display = XOpenDisplay(NULL);

	if (global_x_info->display == NULL){
		RL_Print("XOpenDisplay failed, graphics is not supported\n");
		return;
	} else {
		//RL_Print("XOpenDisplay succeeded: x_dislay = %x\n", global_x_info->display);
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
			if(red_mask == 0x7C00 && green_mask == 0x3E0 && blue_mask == 0x1F)
				global_x_info->sys_pixmap_format = pix_format_bgr555;
			break;
		case 16:
			global_x_info->bpp = 16;
			if(red_mask == 0xF800 && green_mask == 0x7E0 && blue_mask == 0x1F)
				global_x_info->sys_pixmap_format = pix_format_bgr565;
			break;
		case 24:
		case 32:
			global_x_info->bpp = 32;
			if (red_mask == 0xFF0000 && green_mask == 0xFF00 && blue_mask == 0xFF)
				global_x_info->sys_pixmap_format = pix_format_bgra32;
			else if (blue_mask == 0xFF0000 && green_mask == 0xFF00 && red_mask == 0xFF)
				global_x_info->sys_pixmap_format = pix_format_rgba32;
			break;
		defaut:
			break;
	}

	if (global_x_info->sys_pixmap_format == pix_format_undefined) {
		Host_Crash("System Pixmap format couldn't be determined");
	}
#ifdef USE_XSHM
	int ignore, major, minor;
	REBOOL pixmaps;

	/* Check for the XShm extension */
	global_x_info->has_xshm = XQueryExtension(global_x_info->display, "MIT-SHM", &ignore, &ignore, &ignore);
	if (global_x_info->has_xshm) {
		if (XShmQueryVersion(global_x_info->display, &major, &minor, &pixmaps) == True) {
			printf("XShm extention version %d.%d %s shared pixmaps\n",
				   major, minor, (pixmaps == True) ? "with" : "without");
		} else {
			printf("XShm is not supported\n");
		}
	}
#endif

	global_x_info->leader_window = XCreateWindow(global_x_info->display,
												 DefaultRootWindow(global_x_info->display),
												 0, 0, 10, 10, /* x, y, w, h */
												 0, /* borderwidth */
												 CopyFromParent, InputOutput,
												 CopyFromParent, 0, NULL);


}


static void X11_change_state (REBOOL   add,
				 Window window,
				 Atom    state1,
				 Atom    state2)
{
	XClientMessageEvent xclient;
	Atom wm_state = XInternAtom(global_x_info->display, "_NET_WM_STATE", True);
	Window root = DefaultRootWindow(global_x_info->display);

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */
 
	memset (&xclient, 0, sizeof (xclient));
	xclient.type = ClientMessage;
	xclient.window = window;
	xclient.message_type = wm_state;
	xclient.format = 32;
	xclient.data.l[0] = add ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
	xclient.data.l[1] = state1;
	xclient.data.l[2] = state2;
	xclient.data.l[3] = 1; /* source indication */
	xclient.data.l[4] = 0;

	XSendEvent (global_x_info->display, root, False,
				SubstructureRedirectMask | SubstructureNotifyMask,
				(XEvent *)&xclient);
}

static void update_gob_window_state(REBGOB *gob,
									Display *display,
									Window window)
{
	X11_change_state(GET_GOB_FLAG(gob, GOBF_MAXIMIZE),
					 window,
					 XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", True),
					 XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", True));
	//RL_Print("%s fullscreen flag for window %x\n", GET_GOB_FLAG(gob, GOBF_FULLSCREEN) ? "Setting" : "Clearing", window);
	X11_change_state(GET_GOB_FLAG(gob, GOBF_FULLSCREEN),
					 window,
					 XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", True),
					 0);
	X11_change_state(GET_GOB_FLAG(gob, GOBF_HIDDEN),
					 window,
					 XInternAtom(display, "_NET_WM_STATE_HIDDEN", False), 0);

	X11_change_state(GET_GOB_FLAG(gob, GOBF_ACTIVE),
					 window,
					 XInternAtom(display, "_NET_WM_STATE_FOCUSED", False), 0);

	X11_change_state(GET_GOB_FLAG(gob, GOBF_ON_TOP),
					 window,
					 XInternAtom(display, "_NET_WM_STATE_ABOVE", False), 0);
}

int reb_x11_get_window_extens(Display *display,
							  Window window,
							  unsigned *left,
							  unsigned *right,
							  unsigned *top,
							  unsigned *bottom)
{
       Atom     actual_type;
       int      actual_format;
       long     nitems;
       long     bytes;
       long     *data = NULL;
       int      status;
       status = XGetWindowProperty(display,
								   window,
								   XInternAtom(display, "_NET_FRAME_EXTENTS", True),
								   0,
								   (~0L),
								   False,
								   XA_CARDINAL,
								   &actual_type,
								   &actual_format,
								   &nitems,
								   &bytes,
								   (unsigned char**)&data);
       if (status != Success
		   || data == NULL
		   || nitems != 4
		   || actual_type != XA_CARDINAL
		   || actual_format != 32) {
		   if (data) {
			   XFree(data);
		   }
		   return -1;
	   }

	   //left, right, top, bottom
	   if (left) {
		   *left = data[0];
	   }
	   if (right) {
		   *right = data[1];
	   }
	   if (top) {
		   *top = data[2];
	   }
	   if (bottom) {
		   *bottom = data[3];
	   }
	   XFree(data);

	   return 0;
}
/***********************************************************************
**
*/	void OS_Update_Window(REBGOB *gob)
/*
**		Update window parameters.
**
***********************************************************************/
{
	//RL_Print("updating window:");
	REBINT x = GOB_LOG_X_INT(gob);
	REBINT y = GOB_LOG_Y_INT(gob);
	REBINT w = GOB_LOG_W_INT(gob);
	REBINT h = GOB_LOG_H_INT(gob);
	int actual_x, actual_y, actual_w, actual_h, actual_border_width, actual_depth;
	Window root;
	/*
	RL_Print("Updating window %x to (x: %d, y: %d, width: %d, height: %d) from (w %d, h %d)\n", gob, x, y, w, h,
			 GOB_WO_INT(gob), GOB_HO_INT(gob));
	*/
	host_window_t *hw = GOB_HWIN(gob);
	//assert (win != 0);
	if (!hw || global_x_info->display == NULL) {
		return;
	}

	update_gob_window_state(gob, global_x_info->display, hw->x_id);
	Resize_Window(gob, FALSE);
	XGetGeometry(global_x_info->display, hw->x_id, &root, &actual_x, &actual_y, 
				 &actual_w, &actual_h, &actual_border_width, &actual_depth);
	/*
	RL_Print("%s %d, Updating an X window %x for gob %x, x: %d, y: %d, w: %d, h: %d, border: %d, depth: %d\n",
			 __func__, __LINE__, hw->x_id, gob,
			 actual_x, actual_y, actual_w, actual_h,
			 actual_border_width, actual_depth);
			 */
	if (actual_w != w || actual_h != h){
		XResizeWindow(global_x_info->display, hw->x_id, w, h);
	}
	REBGOB *parent_gob = GOB_TMP_OWNER(gob);
	//RL_Print("%s, %d, gob: %x, parent gob: %x, pos: %dx%d, size: %dx%d\n", __func__, __LINE__, gob, parent_gob, x, y, w, h);
	if (parent_gob != NULL) {
			host_window_t *parent_hw = GOB_HWIN(parent_gob);
			if (hw != NULL) {
					Window gob_parent_window = parent_hw->x_id;
					Window child;
					Window x_parent_window, *children;
					int n_children;
					XQueryTree(global_x_info->display, hw->x_id, &root, &x_parent_window, &children, &n_children);
					if (GET_GOB_FLAG(gob, GOBF_POPUP)) {
						/* x, y are in screen coordinates for POPUP windows */
						if (x_parent_window != root) {
							XTranslateCoordinates(global_x_info->display, root, x_parent_window, x, y, &x, &y, &child);
						}
					} else {
						/* x, y are in parent window coordinates */
						XTranslateCoordinates(global_x_info->display, gob_parent_window, x_parent_window, x, y, &x, &y, &child);
					}
			}
	}

	if (x != GOB_XO_INT(gob) || y != GOB_YO_INT(gob)){
		/*
		RL_Print("Moving window: %x from %dx%d to %dx%d\n",
				 hw->x_id, GOB_XO_INT(gob), GOB_YO_INT(gob), x, y);
				 */
		unsigned left = 0, top = 0;
		if (reb_x11_get_window_extens(global_x_info->display,
									  hw->x_id,
									  &left, NULL, &top, NULL) == 0){
			//RL_Print("left: %d, top: %d\n", left, top);
			XMoveWindow(global_x_info->display, hw->x_id, x - left, y - top);
		} else {
			XMoveWindow(global_x_info->display, hw->x_id, x, y);
		}
	}
}

static void set_wm_name(Display *display,
						Window window,
						REBCHR *title)
{
	Atom XA_TITLE = XInternAtom(display, "_NET_WM_NAME", False);
	Atom XA_UTF8_STRING = XInternAtom(display, "UTF8_STRING", False);
	XChangeProperty(display, window, XA_TITLE, XA_UTF8_STRING, 8,
					PropModeReplace, title, strlen(title));
	XStoreName(display, window, title); //backup for non NET Wms
}

static void set_class_hint(Display *display,
						   Window window,
						   REBCHR *title)
{
	XClassHint *class_hint = XAllocClassHint();
	if (class_hint) {
		class_hint->res_name = title;
		class_hint->res_class = "REBOL";
		XSetClassHint(display, window, class_hint);
		XFree(class_hint);
	}
}

static void set_wm_hints(Display *display,
						 Window window)
{
	XWMHints *hints = XAllocWMHints();
	if (hints) {
		hints->flags = WindowGroupHint;
		hints->window_group = global_x_info->leader_window;
		XSetWMHints(display, window, hints);
		XFree(hints);
	}
}

static void set_gob_window_title(REBGOB *gob,
								 Display *display,
								 Window window)
{
	REBCHR *title;
	REBYTE os_string = FALSE;
	XTextProperty title_prop;
	Atom XA_TITLE;
	Atom XA_UTF8_STRING;

	if (IS_GOB_STRING(gob))
        os_string = As_OS_Str(GOB_CONTENT(gob), (REBCHR**)&title);
    else
        title = TXT("REBOL Window");
	
	set_wm_name(display, window, title);
	set_class_hint(display, window, title);

	if (os_string)
		OS_Free(title);
}

static void set_gob_window_size_hints(REBGOB *gob,
									  Display *display,
									  Window window)
{
	REBINT w = GOB_LOG_W_INT(gob);
	REBINT h = GOB_LOG_H_INT(gob);
	XSizeHints *size_hints = XAllocSizeHints();
	if (size_hints) {
		size_hints->flags = PBaseSize;
		size_hints->base_width = w;
		size_hints->base_height = h;
		if (!GET_GOB_FLAG(gob, GOBF_RESIZE)
			&& !GET_GOB_FLAG(gob, GOBF_MAXIMIZE)
			&& !GET_GOB_FLAG(gob, GOBF_FULLSCREEN)) {
			size_hints->flags |= (PMaxSize | PMinSize);
			size_hints->min_width = w;
			size_hints->min_height = h;
			size_hints->max_width = w;
			size_hints->max_height = h;
		}
		XSetWMNormalHints(display, window, size_hints);
		XFree(size_hints);
	}
}

static void set_window_protocols(Display *display,
								 Window window)
{
	Atom wm_protocols[] = {
		XInternAtom(display, "WM_DELETE_WINDOW", True),
		XInternAtom(display, "_NET_WM_PING", True)
	};
	XSetWMProtocols(display, window, wm_protocols, sizeof(wm_protocols)/sizeof(wm_protocols[0]));
}

static void set_wm_client_machine(Display *display,
									  Window window)
{
	XTextProperty client_machine;
	char hostname[HOST_NAME_MAX];
	if (!gethostname(hostname, HOST_NAME_MAX)) {
		client_machine.value = hostname;
		client_machine.encoding = XA_STRING;
		client_machine.format = 8;
		client_machine.nitems = strlen(hostname);
		XSetWMClientMachine(display, window, &client_machine);
	}
}

static void set_wm_pid(Display *display,
					   Window window)
{
	Atom window_pid = XInternAtom(display, "_NET_WM_PID", True);
	if (window_pid) {
		pid_t pid = getpid();
		XChangeProperty(display, window, window_pid, XA_CARDINAL, 32,
						PropModeReplace,
						(unsigned char *)&pid, 1);
	}
}

static void set_window_leader(Display *display,
							  Window window)
{
	Atom XA_WM_CLIENT_LEADER = XInternAtom(display, "WM_CLIENT_LEADER", True);
	if (!global_x_info->leader_window) {
		global_x_info->leader_window = window;
	}
	int status = XChangeProperty(display, window, XA_WM_CLIENT_LEADER, XA_WINDOW, 32,
								 PropModeReplace, (unsigned char*)&(global_x_info->leader_window), 1);
}

static void set_gob_window_type(REBGOB *gob,
								Display *display,
								Window window)
{
	Window parent_window;
	Atom window_type;
	Atom window_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", True);
	if (GET_FLAGS(gob->flags, GOBF_NO_TITLE, GOBF_NO_BORDER)) {
		window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", True);
		host_window_t *hw = GOB_HWIN(GOB_TMP_OWNER(gob));
		if (!hw) {
			Host_Crash("Parent window can't be found\n");
		}
		parent_window = hw->x_id;
		XSetTransientForHint(display, window, parent_window);
	} else if (GET_GOB_FLAG(gob, GOBF_MODAL)) {
		Atom wm_state = XInternAtom(display, "_NET_WM_STATE", True);
		Atom wm_state_modal = XInternAtom(display, "_NET_WM_STATE_MODAL", True);
		host_window_t *hw = GOB_HWIN(GOB_TMP_OWNER(gob));
		if (!hw) {
			Host_Crash("Parent window can't be found\n");
		}
		parent_window = hw->x_id;
		XSetTransientForHint(display, window, parent_window);
		int status = XChangeProperty(display, window, wm_state, XA_ATOM, 32,
									 PropModeReplace, (unsigned char*)&wm_state_modal, 1);
		window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", True);
	} else {
		window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_NORMAL", True);
	}
	XChangeProperty(display, window, window_type_atom, XA_ATOM, 32,
					PropModeReplace,
					(unsigned char *)&window_type, 1);
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

	Window window;
	u32 mask = 0;
	u32 values[6];
	//xcb_drawable_t d;
	
	Display *display = global_x_info->display;

	if (display == NULL) {
		return NULL;
	}
	XSetWindowAttributes swa;
	long swa_mask = CWEventMask;

	Window parent_window;
	Window root;

	//RL_Print("%s, %d, x: %d, y: %d, width: %d, height: %d\n", __func__, __LINE__, x, y, w, h);

	swa.event_mask = ExposureMask 
					| PointerMotionMask 
					| KeyPressMask 
					| KeyReleaseMask
					| ButtonPressMask 
					| ButtonReleaseMask 
					| StructureNotifyMask 
					| FocusChangeMask;

	parent_window = root = DefaultRootWindow(display);
	REBGOB *parent_gob = GOB_TMP_OWNER(gob);
	//RL_Print("%s, %d, gob: %x, parent gob: %x, pos: %dx%d, size: %dx%d\n", __func__, __LINE__, gob, parent_gob, x, y, w, h);
	if (parent_gob != NULL) {
			host_window_t *hw = GOB_HWIN(parent_gob);
			if (hw != NULL) {
					Window gob_parent_window = hw->x_id;
					Window child;
					if (GET_GOB_FLAG(gob, GOBF_POPUP)) {
						/* try to mimic CreateWindowsEx on win32,
						 * x, y are in screen coordinates for POPUP windows, see
						 * http://msdn.microsoft.com/en-us/library/windows/desktop/ms632680%28v=vs.85%29.aspx
						 **/
						if (parent_window != root) {
							XTranslateCoordinates(display, root, parent_window, x, y, &x, &y, &child);
						}
					} else {
						/* x, y are in parent window coordinates */
						XTranslateCoordinates(display, gob_parent_window, parent_window, x, y, &x, &y, &child);
					}
			}
	}

	if (GET_FLAGS(gob->flags, GOBF_NO_TITLE, GOBF_NO_BORDER)) {
		swa.save_under = True;
		swa.override_redirect = True;
		swa.cursor = None;
		swa_mask |= CWSaveUnder | CWOverrideRedirect | CWCursor;
		window = XCreateWindow(display, 
							   parent_window,
							   x, y, w, h,
							   REB_WINDOW_BORDER_WIDTH,
							   CopyFromParent, InputOutput,
							   CopyFromParent, swa_mask,
							   &swa);
	} else {
		window = XCreateWindow(display, 
							   parent_window,
							   x, y, w, h,
							   REB_WINDOW_BORDER_WIDTH,
							   CopyFromParent, InputOutput,
							   CopyFromParent, swa_mask,
							   &swa);
	}

	set_gob_window_type(gob, display, window);
	set_window_leader(display, window);

	set_wm_hints(display, window);

	set_wm_pid(display, window);
	set_wm_client_machine(display, window);
	set_gob_window_title(gob, display, window);

	set_window_protocols(display, window);

	set_gob_window_size_hints(gob, display, window);
	OS_Update_Window(gob);

	windex = Alloc_Window(gob);

	if (windex < 0) Host_Crash("Too many windows");

	host_window_t *hw = OS_Make(sizeof(host_window_t));
	if (hw == NULL) {
		Host_Crash("Not enough memory\n");
	}
	hw->x_id = window;
	hw->old_width = w;
	hw->old_height = h;
	Gob_Windows[windex].win = hw;
	Gob_Windows[windex].compositor = rebcmp_create(Gob_Root, gob);

	if (! GET_GOB_FLAG(gob, GOBF_HIDDEN)) {
		//RL_Print("Mapping %x\n", window);
		XMapWindow(display, window);
	}

	CLEAR_GOB_STATE(gob);
	SET_GOB_STATE(gob, GOBS_NEW);

	SET_GOB_FLAG(gob, GOBF_WINDOW);
	SET_GOB_FLAG(gob, GOBF_ACTIVE);
	SET_GOB_STATE(gob, GOBS_OPEN);

	return hw;
}

/***********************************************************************
**
*/  void OS_Close_Window(REBGOB *gob)
/*
**		Close the window.
**
***********************************************************************/
{
	//RL_Print("Closing %x\n", gob);
	if (GET_GOB_FLAG(gob, GOBF_WINDOW)) {
		if (global_x_info->display == NULL){
			return;
		}
		XSync(global_x_info->display, FALSE); //wait child window to be destroyed and notified
		X_Event_Loop(-1);
		host_window_t *hw = GOB_HWIN(gob);
		if (hw) {
			//RL_Print("Destroying window: %x\n", win);
			XDestroyWindow(global_x_info->display, hw->x_id);
			X_Event_Loop(-1);

			Free_Window(gob);
			OS_Free(hw);
		}
	}
}
