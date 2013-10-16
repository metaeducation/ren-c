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

#define GOB_HWIN(gob)	(Find_Window(gob))
#define GOB_COMPOSITOR(gob)	(Find_Compositor(gob)) //gets handle to window's compositor

//***** Externs *****
extern REBGOBWINDOWS *Gob_Windows;
extern void Free_Window(REBGOB *gob);
extern void* Find_Compositor(REBGOB *gob);
extern REBINT Alloc_Window(REBGOB *gob);
extern void Draw_Window(REBGOB *wingob, REBGOB *gob);

Display *x_display;
//***** Locals *****

#define MAX_WINDOWS 64 //must be in sync with os/host-view.c

REBGOB *Find_Gob_By_Window(Window win)
{
	int i = 0;
	for(i = 0; i < MAX_WINDOWS; i ++ ){
		if (((host_window_t*)Gob_Windows[i].win)->x_window == win){
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
	x_display = XOpenDisplay(NULL);
	if (x_display == NULL){
		RL_Print("XOpenDisplay failed");
	}else{
		RL_Print("XOpenDisplay succeeded: x_dislay = %x\n", x_display);
	}

	xlib_rgb_init (x_display, DefaultScreenOfDisplay(x_display));
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
	int screen_num;
	u32 mask = 0;
	u32 values[6];
	//xcb_drawable_t d;
	Window root;
	XSetWindowAttributes swa;

	host_window_t *reb_host_window;

	RL_Print("x: %d, y: %d, width: %d, height: %d\n", x, y, w, h);
	root = DefaultRootWindow(x_display);
	swa.event_mask  =  ExposureMask | PointerMotionMask | KeyPressMask | KeyReleaseMask| ButtonPressMask |ButtonReleaseMask | StructureNotifyMask;
	window = XCreateWindow(x_display, 
						   root,
						   x, y, w, h,
						   0,
						   CopyFromParent, InputOutput,
						   CopyFromParent, CWEventMask,
						   &swa);

	Atom wmDelete=XInternAtom(x_display, "WM_DELETE_WINDOW", 1);
	XSetWMProtocols(x_display, window, &wmDelete, 1);

	XMapWindow(x_display, window);

	windex = Alloc_Window(gob);

	if (windex < 0) Host_Crash("Too many windows");

	GC gc = XCreateGC(x_display, window, 0, 0);
	screen_num = DefaultScreen(x_display);
	unsigned long black = BlackPixel(x_display, screen_num);
	unsigned long white = WhitePixel(x_display, screen_num);
	XSetBackground(x_display, gc, white);
	XSetForeground(x_display, gc, black);

	host_window_t *ew = OS_Make(sizeof(host_window_t));
	ew->x_window = window;
	ew->x_gc = gc;
	ew->pixbuf_len = w * h * 4; //RGB32;
	ew->pixbuf = OS_Make(ew->pixbuf_len);

	Gob_Windows[windex].win = ew;
	Gob_Windows[windex].compositor = rebcmp_create(Gob_Root, gob);

	//glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
	
	CLEAR_GOB_STATE(gob);
	SET_GOB_STATE(gob, GOBS_NEW);

	SET_GOB_FLAG(gob, GOBF_WINDOW);
	SET_GOB_FLAG(gob, GOBF_ACTIVE);	
	SET_GOB_STATE(gob, GOBS_OPEN);

	return ew;
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
	host_window_t *win = GOB_HWIN(gob);
   	XDestroyWindow    (x_display, win->x_window);
	OS_Free(win);
}
