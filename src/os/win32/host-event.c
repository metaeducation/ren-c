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
**  Title: Windowing Event Handler
**  Author: Carl Sassenrath, Richard Smolak
**  Purpose: This code handles windowing related events.
**	Related: host-window.c, dev-event.c
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

#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0501        // this is needed to be able use SPI_GETWHEELSCROLLLINES etc.
#endif

#include <windows.h>
#include <commctrl.h>	// For WM_MOUSELEAVE event
#include <zmouse.h>
#include <math.h>	//for floor()

//-- Not currently used:
//#include <windowsx.h>
//#include <mmsystem.h>
//#include <winuser.h>

#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(wparam) ((short)HIWORD (wparam))
#endif

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

#ifndef SPI_GETWHEELSCROLLCHARS
#define SPI_GETWHEELSCROLLCHARS 0x006C
#endif

#include "reb-host.h"
#include "host-lib.h"

//***** Constants *****

// Virtual key conversion table, sorted by first column.
const REBCNT Key_To_Event[] = {
        VK_TAB,     EVK_NONE,   //EVK_NONE means it is passed 'as-is'
		VK_PRIOR,	EVK_PAGE_UP,
		VK_NEXT,	EVK_PAGE_DOWN,
		VK_END,		EVK_END,
		VK_HOME,	EVK_HOME,
		VK_LEFT,	EVK_LEFT,
		VK_UP,		EVK_UP,
		VK_RIGHT,	EVK_RIGHT,
		VK_DOWN,	EVK_DOWN,
		VK_INSERT,	EVK_INSERT,
		VK_DELETE,	EVK_DELETE,
		VK_F1,		EVK_F1,
		VK_F2,		EVK_F2,
		VK_F3,		EVK_F3,
		VK_F4,		EVK_F4,
		VK_F5,		EVK_F5,
		VK_F6,		EVK_F6,
		VK_F7,		EVK_F7,
		VK_F8,		EVK_F8,
		VK_F9,		EVK_F9,
		VK_F10,		EVK_F10,
		VK_F11,		EVK_F11,
		VK_F12,		EVK_F12,
		0x7fffffff,	0
};

//***** Externs *****

extern void *Cursor;
extern void Done_Device(int handle, int error);
extern void Paint_Window(HWND window);
extern void Close_Window(REBGOB *gob);
extern REBOOL Resize_Window(REBGOB *gob, REBOOL redraw);
extern HWND Find_Window(REBGOB *gob);
extern BOOL osDialogOpen; //this flag is checked to block rebol events loop when specific non-modal OS dialog(request-file, request-dir etc.) is opened

/***********************************************************************
**
**	Local Functions
**
***********************************************************************/

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

static void Add_File_Events(REBGOB *gob, REBINT flags, HDROP drop)
{
	REBEVT evt;
	REBINT num;
	REBINT len;
	REBINT i;
	REBCHR* buf;
	POINT xy;

	//Get the mouse position
	DragQueryPoint(drop, &xy);

	evt.type  = EVT_DROP_FILE;
	evt.flags = (u8) (flags | (1<<EVF_HAS_XY));
	evt.model = EVM_GUI;
	evt.data = xy.x | xy.y<<16;


	num = DragQueryFile(drop, -1, NULL, 0);

	for (i = 0; i < num; i++){
		len = DragQueryFile(drop, i, NULL, 0);
		buf = OS_Make(len+1);
		DragQueryFile(drop, i, buf, len+1);
		//Reb_Print("DROP: %s", buf);
		buf[len] = 0;
		// ?! convert to REBOL format? E.g.: evt.ser = OS_To_REBOL_File(buf, &len);
		OS_Free(buf);
		if (!RL_Event(&evt)) break;	// queue is full
	}
}

static Check_Modifiers(REBINT flags)
{
	if (GetKeyState(VK_SHIFT) < 0) flags |= (1<<EVF_SHIFT);
	if (GetKeyState(VK_CONTROL) < 0) flags |= (1<<EVF_CONTROL);
	return flags;
}


/***********************************************************************
**
*/	LRESULT CALLBACK REBOL_Window_Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM xy)
/*
**		A Window_Proc() message handler. Simply translate Windows
**		messages into a generic form that REBOL processes.
**
***********************************************************************/
{
	LPARAM xyd;
	REBGOB *gob;
	REBCNT flags = 0;
	REBCNT i;
	REBCNT mw_num_lines;  // mouse wheel lines setting
	// In order to trace resizing, we need these state variables. It is
	// assumed that we are not re-entrant during this operation. In Win32
	// resizing is a modal loop and prevents it being a problem.
	static LPARAM last_xy = 0;
	static REBINT mode = 0;
	static REBINT wheel_mode = 0;
    static REBYTE keyboardState[256];
    static REBCHR buf[2];

	gob = (REBGOB *)GetWindowLong(hwnd, GWL_USERDATA);

//    RL_Print("MSG: %d , %d\n", msg,wParam);

	// Not a REBOL window (or early creation), or specific OS dialog is opened:
	if (!gob || !IS_WINDOW(gob) || osDialogOpen) {
		switch(msg) {
			case WM_PAINT:
				Paint_Window(hwnd);
				break;
			case WM_CLOSE:
				DestroyWindow(hwnd);
				break;
			case WM_DESTROY:
				PostQuitMessage(0);
				break;
			default:
				// Default processing that we do not care about:
				return DefWindowProc(hwnd, msg, wParam, xy);
		}
		return 0;
	}

	//downscaled, resolution independent xy event position
	xyd = (ROUND_TO_INT(PHYS_COORD_X((i16)LOWORD(xy)))) + (ROUND_TO_INT(PHYS_COORD_Y((i16)HIWORD(xy))) << 16);
	// Handle message:
	switch(msg)
	{
		case WM_PAINT:
			Paint_Window(hwnd);
			break;

		case WM_MOUSEMOVE:
			Add_Event_XY(gob, EVT_MOVE, xyd, flags);
			//if (!(WIN_FLAGS(wp) & WINDOW_TRACK_LEAVE))
			//	Track_Mouse_Leave(wp);
			break;

		case WM_SIZE:
//			RL_Print("SIZE %d\n", mode);

            if (wParam == SIZE_MINIMIZED) {
//				RL_Print("MINIMIZE!\n");
				//Invalidate the size but not win buffer
				gob->old_size.x = 0;
				gob->old_size.y = 0;
				if (!GET_GOB_FLAG(gob, GOBF_MINIMIZE)){
					SET_GOB_FLAG(gob, GOBF_MINIMIZE);
					CLR_GOB_FLAGS(gob, GOBF_RESTORE, GOBF_MAXIMIZE);
				}
				Add_Event_XY(gob, EVT_MINIMIZE, xyd, flags);
			} else {
//				RL_Print("SIZE OTHER!\n");
				gob->size.x = (i16)LOWORD(xyd);

				gob->size.y = (i16)HIWORD(xyd);

				last_xy = xyd;
				if (mode) {
					//Resize and redraw the window buffer (when resize dragging)
					Resize_Window(gob, TRUE);
					mode = EVT_RESIZE;
					break;
				} else {
//					RL_Print("resize WINDOW\n");
					//Resize only the window buffer (when win gob size changed by REBOL code or using min/max buttons)
					if (!Resize_Window(gob, FALSE)){
						//size has been changed programatically - return only 'resize event
						Add_Event_XY(gob, EVT_RESIZE, xyd, flags);
						break;
					}
				}
				//Otherwise send combo of 'resize + maximize/restore events
				if (wParam == SIZE_MAXIMIZED) {
//					RL_Print("MAXIMIZE!\n");
					i = EVT_MAXIMIZE;
					if (!GET_GOB_FLAG(gob, GOBF_MAXIMIZE)){
						SET_GOB_FLAG(gob, GOBF_MAXIMIZE);
						CLR_GOB_FLAGS(gob, GOBF_RESTORE, GOBF_MINIMIZE);
					}
				} else if (wParam == SIZE_RESTORED) {
//					RL_Print("RESTORE!\n");
					i = EVT_RESTORE;
					if (!GET_GOB_FLAG(gob, GOBF_RESTORE)){
						SET_GOB_FLAG(gob, GOBF_RESTORE);
						CLR_GOB_FLAGS(gob, GOBF_MAXIMIZE, GOBF_MINIMIZE);
					}
				}
				else i = 0;
				Add_Event_XY(gob, EVT_RESIZE, xyd, flags);
				if (i) Add_Event_XY(gob, i, xyd, flags);
			}
			break;

		case WM_MOVE:
			// Minimize and maximize call this w/o mode set.
			gob->offset.x = (i16)LOWORD(xyd);

			gob->offset.y = (i16)HIWORD(xyd);

			last_xy = xyd;
			if (mode) mode = EVT_OFFSET;
			else Add_Event_XY(gob, EVT_OFFSET, xyd, flags);
			break;

		case WM_ENTERSIZEMOVE:
			mode = -1; // possible to ENTER and EXIT w/o SIZE change
			break;

		case WM_EXITSIZEMOVE:
			if (mode > 0) Add_Event_XY(gob, mode, last_xy, flags);
			mode = 0;
			break;

		case WM_MOUSELEAVE:
			// Get cursor position, not the one given in message:
			//GetCursorPos(&x_y);
			//ScreenToClient(hwnd, &x_y);
			//xy = (x_y.y << 16) + (x_y.x & 0xffff);
			Add_Event_XY(gob, EVT_MOVE, xyd, flags);
			// WIN_FLAGS(wp) &= ~WINDOW_TRACK_LEAVE;
			break;

        //this can be sent by some mouse/trackpad drivers to fake the horizontal wheel
		case WM_HSCROLL:
            //so we are 'faking' the horizontal events as well
            if (wheel_mode == msg || !wheel_mode)
                wheel_mode = msg;
            else
                return 0;

            mw_num_lines = 3; //default
            SystemParametersInfo(SPI_GETWHEELSCROLLCHARS,0, &mw_num_lines, 0);

            switch(LOWORD(wParam))
            {
                case SB_LINELEFT:
                    i = EVT_SCROLL_LINE;
                    wParam = mw_num_lines;
                    break;
                case SB_LINERIGHT:
                    i = EVT_SCROLL_LINE;
                    wParam = -mw_num_lines;
                    break;
                case SB_PAGELEFT:
                    i = EVT_SCROLL_PAGE;
                    wParam = 1;
                    break;
                case SB_PAGERIGHT:
                    i = EVT_SCROLL_PAGE;
                    wParam = -1;
                    break;
                default:
                    return 0;
            }
			Add_Event_XY(gob, i, (u16)wParam , flags);
            break;

        case WM_MOUSEHWHEEL:
            if (wheel_mode == msg || !wheel_mode)
                wheel_mode = msg;
            else
                return 0;

            mw_num_lines = 3; //default
			SystemParametersInfo(SPI_GETWHEELSCROLLCHARS,0, &mw_num_lines, 0);
			if (LOWORD(wParam) == MK_CONTROL || mw_num_lines > WHEEL_DELTA) {
				Add_Event_XY(gob, EVT_SCROLL_PAGE, (u16)-(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) , flags);
			} else {
				Add_Event_XY(gob, EVT_SCROLL_LINE, (u16)-((GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) * mw_num_lines) , flags);
			}
			break;

		case WM_MOUSEWHEEL:
			SystemParametersInfo(SPI_GETWHEELSCROLLLINES,0, &mw_num_lines, 0);
			if (LOWORD(wParam) == MK_CONTROL || mw_num_lines > WHEEL_DELTA) {
				Add_Event_XY(gob, EVT_SCROLL_PAGE, (GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) << 16, flags);
			} else {
				Add_Event_XY(gob, EVT_SCROLL_LINE, (((GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) << 16) * mw_num_lines) , flags);
			}
			break;

		case WM_TIMER:
			//Add_Event_XY(gob, EVT_TIME, xy, flags);
			break;

		case WM_SETCURSOR:
			if (LOWORD(xy) == 1) {
				OS_Set_Cursor(Cursor);
				return TRUE;
			} else goto default_case;

		case WM_LBUTTONDBLCLK:
			SET_FLAG(flags, EVF_DOUBLE);
		case WM_LBUTTONDOWN:
			//if (!WIN_CAPTURED(wp)) {
			flags = Check_Modifiers(flags);
			Add_Event_XY(gob, EVT_DOWN, xyd, flags);
			SetCapture(hwnd);
			//WIN_CAPTURED(wp) = EVT_BTN1_UP;
			break;

		case WM_LBUTTONUP:
			//if (WIN_CAPTURED(wp) == EVT_BTN1_UP) {
			flags = Check_Modifiers(flags);
			Add_Event_XY(gob, EVT_UP, xyd, flags);
			ReleaseCapture();
			//WIN_CAPTURED(wp) = 0;
			break;

		case WM_RBUTTONDBLCLK:
			SET_FLAG(flags, EVF_DOUBLE);
		case WM_RBUTTONDOWN:
			//if (!WIN_CAPTURED(wp)) {
			flags = Check_Modifiers(flags);
			Add_Event_XY(gob, EVT_ALT_DOWN, xyd, flags);
			SetCapture(hwnd);
			//WIN_CAPTURED(wp) = EVT_BTN2_UP;
			break;

		case WM_RBUTTONUP:
			//if (WIN_CAPTURED(wp) == EVT_BTN2_UP) {
			flags = Check_Modifiers(flags);
			Add_Event_XY(gob, EVT_ALT_UP, xyd, flags);
			ReleaseCapture();
			//WIN_CAPTURED(wp) = 0;
			break;

		case WM_MBUTTONDBLCLK:
			SET_FLAG(flags, EVF_DOUBLE);
		case WM_MBUTTONDOWN:
			//if (!WIN_CAPTURED(wp)) {
			flags = Check_Modifiers(flags);
			Add_Event_XY(gob, EVT_AUX_DOWN, xyd, flags);
			SetCapture(hwnd);
			break;

		case WM_MBUTTONUP:
			//if (WIN_CAPTURED(wp) == EVT_BTN2_UP) {
			flags = Check_Modifiers(flags);
			Add_Event_XY(gob, EVT_AUX_UP, xyd, flags);
			ReleaseCapture();
			break;

		case WM_KEYDOWN:
			// Note: key repeat may cause multiple downs before an up.
		case WM_KEYUP:
//enable this once we have available ALT word in event/flags
/*
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
*/
			flags = Check_Modifiers(flags);

			for (i = 0; Key_To_Event[i] && wParam > Key_To_Event[i]; i += 2);

			if (wParam == Key_To_Event[i])
                //handle virtual keys
                if (i = Key_To_Event[i+1])
                    i = i << 16;
                else
                    i = wParam; //pass VK as-is
            else {
                //handle character keys
                i = 0;
                GetKeyboardState(keyboardState);
                if (ToUnicode(wParam,MapVirtualKey(wParam,2),keyboardState,buf,2,(msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)))
                    i = (REBINT)*buf;
            }

            //finally generate key event
            if (i) {
                if (i == 127) i = 8; // Windows weirdness of converting ctrl-backspace to delete
                Add_Event_Key(gob, (msg==WM_KEYDOWN || msg == WM_SYSKEYDOWN) ? EVT_KEY : EVT_KEY_UP, i, flags);
            }
			break;

        case WM_ACTIVATE:
            switch (wParam)
            {
                case WA_ACTIVE:
                case WA_CLICKACTIVE:
                    SetFocus(Find_Window(gob));
                    SET_GOB_STATE(gob, GOBS_ACTIVE);
                    Add_Event_XY(gob, EVT_ACTIVE, 0, flags);
                    break;
                case WA_INACTIVE:
                    CLR_GOB_STATE(gob, GOBS_ACTIVE);
                    Add_Event_XY(gob, EVT_INACTIVE, 0, flags);
                    break;
            }
            break;

		case WM_DROPFILES:
			Add_File_Events(gob, flags, (HDROP)wParam);
			break;

		case WM_CLOSE:
			Add_Event_XY(gob, EVT_CLOSE, xyd, flags);
//			Close_Window(gob);	// Needs to be removed - should be done by REBOL event handling
//			DestroyWindow(hwnd);// This is done in Close_Window()
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		default:
		default_case:
			return DefWindowProc(hwnd, msg, wParam, xy);
	}
	return 0;
}
