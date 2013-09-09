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
**  Title: Win32 Windowing support
**  Author: Carl Sassenrath, Richard Smolak
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

#ifndef WINVER
#define WINVER 0x0501        // this is needed to be able use WINDOWINFO struct etc.
#endif

#include <windows.h>
#include <math.h>

#include "reb-host.h"
//#include "host-lib.h"

#include "host-compositor.h"

//***** Constants *****

#define GOB_HWIN(gob)	((HWND)Find_Window(gob))
#define GOB_COMPOSITOR(gob)	(Find_Compositor(gob)) //gets handle to window's compositor

//***** Externs *****

extern HINSTANCE App_Instance;		// Set by winmain function
extern LRESULT CALLBACK REBOL_Window_Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern REBGOBWINDOWS *Gob_Windows;
extern void Free_Window(REBGOB *gob);
extern void* Find_Compositor(REBGOB *gob);
extern REBINT Alloc_Window(REBGOB *gob);
extern void Draw_Window(REBGOB *wingob, REBGOB *gob);

//***** Locals *****

static const REBCHR *Window_Class_Name = TXT("REBOLWindow");
static REBXYF Zero_Pair = {0, 0};

extern void *Cursor;

//**********************************************************************
//** Helper Functions **************************************************
//**********************************************************************

/***********************************************************************
**
*/  static void Register_Window()
/*
**      Register the window class.
**
**      Note: Executed in OS_Init_Windows code.
**
***********************************************************************/
{
	WNDCLASSEX wc;

	wc.cbSize        = sizeof(wc);
	wc.lpszClassName = Window_Class_Name;
	wc.hInstance     = App_Instance;
	wc.lpfnWndProc   = REBOL_Window_Proc;

	wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.lpszMenuName  = NULL;

	wc.hIconSm = LoadImage(App_Instance, // small class icon
		MAKEINTRESOURCE(5),
		IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON),
		GetSystemMetrics(SM_CYSMICON),
		LR_DEFAULTCOLOR
	);

	// If not already registered:
	//if (!GetClassInfo(App_Instance, Window_Class_Name, &wclass))
	//  RegisterClass(&wclass);

	if (!RegisterClassEx(&wc)) Host_Crash("Cannot register window");

}


/***********************************************************************
**
*/  BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
/*
**		Callback function which enables/disables events in windows
**		specified by lParam
**
**		This function is used by Win API EnumWindows() call.
**
***********************************************************************/
{
	if (GetParent(hwnd) == (HWND)lParam){
		if (IsWindowEnabled(hwnd))
			EnableWindow(hwnd, FALSE);
		else
			EnableWindow(hwnd, TRUE);
	}

	return TRUE;
}


/***********************************************************************
**
*/	void Paint_Window(HWND window)
/*
**		Repaint the window by redrawing all the gobs.
**		It just blits the whole window buffer.
**
***********************************************************************/
{
	PAINTSTRUCT ps;
	REBGOB *gob;

	gob = (REBGOB *)GetWindowLong(window, GWL_USERDATA);

	if (gob) {

		BeginPaint(window, (LPPAINTSTRUCT) &ps);

#ifdef AGG_OPENGL
		Draw_Window(gob, gob);
#else
		rebcmp_blit(GOB_COMPOSITOR(gob));
#endif

		EndPaint(window, (LPPAINTSTRUCT) &ps);
	}
}

//**********************************************************************
//** OSAL Library Functions ********************************************
//**********************************************************************

/***********************************************************************
**
*/	void OS_Init_Windows()
/*
**  Initialize special variables of the graphics subsystem.
**
***********************************************************************/
{
	Register_Window();
	Cursor = (void*)LoadCursor(NULL, IDC_ARROW);
}

/***********************************************************************
**
*/	void OS_Update_Window(REBGOB *gob)
/*
**		Update window parameters.
**
***********************************************************************/
{
	RECT r;
	REBCNT opts = 0;
	HWND window;
	WINDOWINFO wi;
	REBCHR *title;
	REBYTE osString = FALSE;
	REBINT x, y, w, h;
	
	wi.cbSize = sizeof(WINDOWINFO);

	if (!IS_WINDOW(gob)) return;

	window = GOB_HWIN(gob);

	x = GOB_LOG_X_INT(gob);
	y = GOB_LOG_Y_INT(gob);
	w = GOB_LOG_W_INT(gob);
	h = GOB_LOG_H_INT(gob);
	
	if ((x == GOB_XO_INT(gob)) && (y == GOB_YO_INT(gob)))
		opts |= SWP_NOMOVE;
		
	if ((w == GOB_WO_INT(gob)) && (h == GOB_HO_INT(gob)))
		opts |= SWP_NOSIZE;
	else {
		//Resize window and/or buffer in case win size changed programatically
		Resize_Window(gob, FALSE);
	}

	//Get the new window size together with borders, tilebar etc.
	GetWindowInfo(window, &wi);
	r.left   = x;
	r.right  = r.left + w;
	r.top    = y;
	r.bottom = r.top + h;
	AdjustWindowRect(&r, wi.dwStyle, FALSE);

	//Set the new size
	SetWindowPos(window, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, opts | SWP_NOZORDER | SWP_NOACTIVATE);

	if (IS_GOB_STRING(gob)){
        osString = As_OS_Str(GOB_CONTENT(gob), (REBCHR**)&title);
		SetWindowText(window, title);
		//don't let the string leak!
		if (osString) OS_Free(title);
	}

//	RL_Print("Win Flags: rs %d mi %d mx %d ac %d\n", GET_GOB_FLAG(gob, GOBF_RESTORE),GET_GOB_FLAG(gob, GOBF_MINIMIZE),GET_GOB_FLAG(gob, GOBF_MAXIMIZE), GET_GOB_FLAG(gob, GOBF_ACTIVE));
	ShowWindow(
        window, (GET_GOB_FLAG(gob, GOBF_RESTORE)) ? SW_RESTORE
            : GET_GOB_FLAG(gob, GOBF_MINIMIZE) ? SW_MINIMIZE
            : GET_GOB_FLAG(gob, GOBF_MAXIMIZE) ? SW_MAXIMIZE
            : SW_SHOWNOACTIVATE
	);

	if (GET_GOB_FLAG(gob, GOBF_ACTIVE)) {
		CLR_GOB_FLAG(gob, GOBF_ACTIVE);	
		SetForegroundWindow(window);
	}

}

/***********************************************************************
**
*/  void* OS_Open_Window(REBGOB *gob)
/*
**      Initialize the graphics window.
**
**		The window handle is returned, but not expected to be used
**		other than for debugging conditions.
**
***********************************************************************/
{
	REBINT options;
	REBINT windex;
	HWND window;
	REBCHR *title;
#ifdef AGG_OPENGL
	int x, y, w, h, ow, oh;
#else
	int x, y, w, h;
#endif
	HWND parent = NULL;
	REBYTE osString = FALSE;
    REBPAR metric;

	windex = Alloc_Window(gob);
	if (windex < 0) Host_Crash("Too many windows");

	CLEAR_GOB_STATE(gob);
	
	x = GOB_LOG_X_INT(gob);
	y = GOB_LOG_Y_INT(gob);
#ifdef AGG_OPENGL
	w = ow = GOB_LOG_W_INT(gob);
	h = oh = GOB_LOG_H_INT(gob);
#else	
	w = GOB_LOG_W_INT(gob);
	h = GOB_LOG_H_INT(gob);
#endif
	SET_GOB_STATE(gob, GOBS_NEW);

	// Setup window options:

	options = WS_POPUP;

	if (!GET_FLAGS(gob->flags, GOBF_NO_TITLE, GOBF_NO_BORDER)) {
	    metric.y = GetSystemMetrics(SM_CYCAPTION);
		options |= WS_MINIMIZEBOX | WS_CAPTION | WS_SYSMENU;
		h += metric.y;
		y -= metric.y;
	}

	if (GET_GOB_FLAG(gob, GOBF_RESIZE)) {
	    metric.x = GetSystemMetrics(SM_CXSIZEFRAME);
	    metric.y = GetSystemMetrics(SM_CYSIZEFRAME);
		options |= WS_SIZEBOX | WS_BORDER;
		x -= metric.x;
		y -= metric.y;
		w += metric.x * 2;
		h += metric.y * 2;
		if (!GET_GOB_FLAG(gob, GOBF_NO_TITLE))
			options |= WS_MAXIMIZEBOX;
	}
	else if (!GET_GOB_FLAG(gob, GOBF_NO_BORDER)) {
	    metric.x = GetSystemMetrics(SM_CXFIXEDFRAME);
	    metric.y = GetSystemMetrics(SM_CYFIXEDFRAME);
		options |= WS_BORDER;
		if (!GET_GOB_FLAG(gob, GOBF_NO_TITLE)){
			x -= metric.x;
			y -= metric.y;
			w += metric.x * 2;
			h += metric.y * 2;
		}
	}

	if (IS_GOB_STRING(gob))
        osString = As_OS_Str(GOB_CONTENT(gob), (REBCHR**)&title);
    else
        title = TXT("REBOL Window");

	if (GET_GOB_FLAG(gob, GOBF_POPUP)) {
		parent = GOB_HWIN(GOB_TMP_OWNER(gob));
		if (GET_GOB_FLAG(gob, GOBF_MODAL)) {
			EnableWindow(parent, FALSE);
			EnumWindows(EnumWindowsProc, (LPARAM)parent);
		}
	}

	// Create the window:
	window = CreateWindowEx(
		WS_EX_WINDOWEDGE,
		Window_Class_Name,
		title,
		options,
		x, y, w, h,
		parent,
		NULL, App_Instance, NULL
	);

#ifdef AGG_OPENGL
        HDC hDC = GetDC(window);

        PIXELFORMATDESCRIPTOR pfd;
        ZeroMemory( &pfd, sizeof( pfd ) );
        pfd.nSize = sizeof( pfd );
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
                      PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cDepthBits = 16;
        pfd.iLayerType = PFD_MAIN_PLANE;
        int iFormat = ChoosePixelFormat( hDC, &pfd );
        SetPixelFormat( hDC, iFormat, &pfd );

        HGLRC hRC;
        hRC = wglCreateContext( hDC );
        wglMakeCurrent( hDC, hRC );

       	glViewport(0, 0, ow, oh);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
//        if (m_flip_y){
//            glOrtho(0 , w, 0, h, -10000, 10000);
//        } else {
            glOrtho(0 , ow, oh, 0, -10000, 10000);
//        }

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.375, 0.375, 0.0);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnableClientState (GL_COLOR_ARRAY);
        glEnableClientState (GL_VERTEX_ARRAY);

		glClear(GL_COLOR_BUFFER_BIT);

//        txtId = EmptyTexture(1024,1024);
#endif
    //don't let the string leak!
    if (osString) OS_Free(title);
	if (!window) Host_Crash("CreateWindow failed");

	// Enable drag and drop
	if (GET_GOB_FLAG(gob, GOBF_DROPABLE))
		DragAcceptFiles(window, TRUE);

	Gob_Windows[windex].win = window;
	Gob_Windows[windex].compositor = rebcmp_create(Gob_Root, gob);
	
	SET_GOB_FLAG(gob, GOBF_WINDOW);
	SET_GOB_FLAG(gob, GOBF_ACTIVE);	
	SET_GOB_STATE(gob, GOBS_OPEN);

	// Provide pointer from window back to REBOL window:
	SetWindowLong(window, GWL_USERDATA, (long)gob);

    if (!GET_GOB_FLAG(gob, GOBF_HIDDEN)) {
        if (GET_GOB_FLAG(gob, GOBF_ON_TOP)) SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        OS_Update_Window(gob);
    }

#ifdef AGG_OPENGL
	setVSync(0);
#endif
	return window;
}

/***********************************************************************
**
*/  void OS_Close_Window(REBGOB *gob)
/*
**		Close the window.
**
***********************************************************************/
{
	HWND parent = NULL;
	if (GET_GOB_FLAG(gob, GOBF_WINDOW) && Find_Window(gob)) {
		if (GET_GOB_FLAG(gob, GOBF_MODAL)) {
			parent = GetParent(GOB_HWIN(gob));
			if (parent) {
				EnableWindow(parent, TRUE);
				EnumWindows(EnumWindowsProc, (LPARAM)parent);
			}
		}
		DestroyWindow(GOB_HWIN(gob));
		CLR_GOB_FLAG(gob, GOBF_WINDOW);
		CLEAR_GOB_STATE(gob); // set here or in the destroy?
		Free_Window(gob);
	}
}
