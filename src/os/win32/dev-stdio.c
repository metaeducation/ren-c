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
**  Title: Device: Standard I/O for Win32
**  Author: Carl Sassenrath
**  Purpose:
**      Provides basic I/O streams support for redirection and
**      opening a console window if necessary.
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
#include <windows.h>
#include <process.h>

#include <fcntl.h>
#include <io.h>

#include "reb-host.h"

#define BUF_SIZE (16*1024)		// MS restrictions apply

#define SF_DEV_NULL 31			// local flag to mark NULL device

#define CONSOLE_MODES ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT \
		| 0x0040 | 0x0020  // quick edit and insert mode (not defined in VC6)

static HANDLE Std_Out = 0;
static HANDLE Std_Inp = 0;
static HANDLE Std_Echo = 0;
static wchar_t *Std_Buf = 0;		// for input and output

static BOOL Redir_Out = 0;
static BOOL Redir_Inp = 0;

static BOOL Con_Out = 1;		//controls the console text output

// Special access:
extern REBDEV *Devices[];


//**********************************************************************

BOOL WINAPI Handle_Break(DWORD dwCtrlType)
{
	// Handle the MS CMD console CTRL-C, BREAK, and other events:
	if (dwCtrlType >= CTRL_CLOSE_EVENT) OS_Exit(100); // close button, shutdown, etc.
	RL_Escape(0);
	return TRUE;	// We handled it
}

#ifdef DEBUG_METHOD
// Because this file deals with stdio, we must avoid using stdio for debug.
// This funtion is of use wne needed.
static dbgout(char *fmt, int d, char *s)
{
	char buf[255];
	FILE *f = fopen("dbgout.txt", "w");
	sprintf(buf, fmt, d, s);
	fwrite(buf, strlen(buf), 1, f);
	fclose(f);
}
// example: dbgout("handle: %x %s\n", hdl, name);
#endif

static void close_stdio(void)
{
	if (Std_Buf) {
		OS_FREE(Std_Buf);
		Std_Buf = 0;
		//FreeConsole();  // problem: causes a delay
	}
	if (Std_Echo) {
		CloseHandle(Std_Echo);
		Std_Echo = 0;
	}
}

//accessor function to enable/disable output print to console
void Console_Output(BOOL state)
{
    Con_Out = state;
}

HWND Get_Console_Window()
{
    HMODULE h = LoadLibraryW(L"kernel32.dll");
	HWND result = (HWND)(HWND (WINAPI *)())GetProcAddress(h, "GetConsoleWindow")();
	FreeLibrary(h);
	return result;
}

BOOL Init_Console()
{
    if (!Std_Out && Con_Out){

        const wchar_t *title = L"REBOL 3";
        HWND win;

        if (!AllocConsole()) {
            return FALSE;
	}

        SetConsoleTitle(title);

        // The goof-balls at MS seem to require this:
        // See: http://support.microsoft.com/kb/124103
        Sleep(40);
        win = Get_Console_Window();

        if (win) {
            SetForegroundWindow(win);
            BringWindowToTop(win);
	}

        // Get the new stdio handles:
        Std_Out = GetStdHandle(STD_OUTPUT_HANDLE);

        if (!Redir_Inp)	{
            Std_Inp = GetStdHandle(STD_INPUT_HANDLE);
            // Make the Win32 console a bit smarter by default:
            SetConsoleMode(Std_Inp, CONSOLE_MODES);
        }

		Std_Buf = OS_ALLOC_ARRAY(wchar_t, BUF_SIZE);

		// Handle stdio CTRL-C interrupt:
		SetConsoleCtrlHandler(Handle_Break, TRUE);
        return TRUE;
    }
    return FALSE;
}

// show/hide console window - should be converted to OS_* function to be called by native func?
void Console_Window(BOOL show)
{
    HWND win = Get_Console_Window();
    Console_Output(TRUE);
    if (win)
        ShowWindow( win, (show) ? SW_SHOW : SW_HIDE );
    else if (show)
        Init_Console();
}

/***********************************************************************
**
*/	DEVICE_CMD Quit_IO(REBREQ *dr)
/*
***********************************************************************/
{
	REBDEV *dev = (REBDEV*)dr; // just to keep compiler happy above

	close_stdio();
	//if (GET_FLAG(dev->flags, RDF_OPEN)) FreeConsole();
	CLR_FLAG(dev->flags, RDF_OPEN);
	return DR_DONE;
}

/***********************************************************************
**
*/	DEVICE_CMD Open_IO(REBREQ *req)
/*
***********************************************************************/
{
	REBDEV *dev;

	dev = Devices[req->device];

	// Avoid opening the console twice (compare dev and req flags):
	if (GET_FLAG(dev->flags, RDF_OPEN)) {
		// Device was opened earlier as null, so req must have that flag:
		if (GET_FLAG(dev->flags, SF_DEV_NULL))
			SET_FLAG(req->modes, RDM_NULL);
		SET_FLAG(req->flags, RRF_OPEN);
		return DR_DONE; // Do not do it again
	}

	if (!GET_FLAG(req->modes, RDM_NULL)) {

		// Get the raw stdio handles:
		Std_Out = GetStdHandle(STD_OUTPUT_HANDLE);
		Std_Inp = GetStdHandle(STD_INPUT_HANDLE);
		//Std_Err = GetStdHandle(STD_ERROR_HANDLE);
		Std_Echo = 0;

		Redir_Out = (GetFileType(Std_Out) != 0);
		Redir_Inp = (GetFileType(Std_Inp) != 0);

 		// Output not redirected, reset out handle
		if (!Redir_Out) {
		    Std_Out = 0;
			}
		}
	else
		SET_FLAG(dev->flags, SF_DEV_NULL);

	SET_FLAG(req->flags, RRF_OPEN);
	SET_FLAG(dev->flags, RDF_OPEN);

	return DR_DONE;
}


/***********************************************************************
**
*/	DEVICE_CMD Close_IO(REBREQ *req)
/*
 ***********************************************************************/
{
	REBDEV *dev = Devices[req->device];

	close_stdio();

	CLR_FLAG(dev->flags, RRF_OPEN);

	return DR_DONE;
}


/***********************************************************************
**
*/	DEVICE_CMD Write_IO(REBREQ *req)
/*
**		Low level "raw" standard output function.
**
**		Allowed to restrict the write to a max OS buffer size.
**
**		Returns the number of chars written.
**
***********************************************************************/
{
    DWORD len;
    DWORD total = 0;
	BOOL ok = FALSE;

	if (GET_FLAG(req->modes, RDM_NULL)) {
		req->actual = req->length;
		return DR_DONE;
	}

    Init_Console();

	if (Std_Out && Con_Out) {

		if (Redir_Out) { // Always UTF-8
			ok = WriteFile(Std_Out, req->common.data, req->length, &total, 0);
		}
		else {
			// Convert UTF-8 buffer to Win32 wide-char format for console.
			// Thankfully, MS provides something other than mbstowcs();
			// however, if our buffer overflows, it's an error. There's no
			// efficient way at this level to split-up the input data,
			// because its UTF-8 with variable char sizes.
            len = MultiByteToWideChar(CP_UTF8, 0, s_cast(req->common.data), req->length, Std_Buf, BUF_SIZE);
			if (len > 0) // no error
				ok = WriteConsoleW(Std_Out, Std_Buf, len, &total, 0);
		}

		if (!ok) {
			req->error = GetLastError();
			return DR_ERROR;
		}

		req->actual = req->length;  // do not use "total" (can be byte or wide)

		//if (GET_FLAG(req->flags, RRF_FLUSH)) {
		//	FLUSH();
		//}
	}

	if (Std_Echo) {	// always UTF-8
		WriteFile(Std_Echo, req->common.data, req->length, &total, 0);
		//FlushFileBuffers(Std_Echo);
	}

	return DR_DONE;
}


/***********************************************************************
**
*/	DEVICE_CMD Read_IO(REBREQ *req)
/*
**		Low level "raw" standard input function.
**
**		The request buffer must be long enough to hold result.
**
**		Result is NOT terminated (the actual field has length.)
**
***********************************************************************/
{
    DWORD total = 0;
    DWORD len;
	BOOL ok;

	if (GET_FLAG(req->modes, RDM_NULL)) {
		req->common.data[0] = 0;
		return DR_DONE;
	}

	req->actual = 0;

	if (Std_Inp) {

		if (Redir_Inp) { // always UTF-8
			len = MIN(req->length, BUF_SIZE);
			ok = ReadFile(Std_Inp, req->common.data, len, &total, 0);
		}
		else {
			ok = ReadConsoleW(Std_Inp, Std_Buf, BUF_SIZE-1, &total, 0);
			if (ok) {
                total = WideCharToMultiByte(CP_UTF8, 0, Std_Buf, total, s_cast(req->common.data), req->length, 0, 0);
				if (!total) ok = FALSE;
			}
		}

		if (!ok) {
			req->error = GetLastError();
			return DR_ERROR;
		}

		req->actual = total;
	}

	return DR_DONE;
}


/***********************************************************************
**
*/	DEVICE_CMD Open_Echo(REBREQ *req)
/*
**		Open a file for low-level console echo (output).
**
***********************************************************************/
{
	if (Std_Echo) {
		CloseHandle(Std_Echo);
		Std_Echo = 0;
	}

	if (req->special.file.path) {
		Std_Echo = CreateFile(req->special.file.path, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, CREATE_ALWAYS, 0, 0);
		if (Std_Echo == INVALID_HANDLE_VALUE) {
			Std_Echo = 0;
			req->error = GetLastError();
			return DR_ERROR;
		}
	}

	return DR_DONE;
}


/***********************************************************************
**
**	Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] =
{
	0,	// init
	Quit_IO,
	Open_IO,
	Close_IO,
	Read_IO,
	Write_IO,
	0,	// poll
	0,	// connect
	0,	// query
	0,	// modify
	Open_Echo,	// CREATE used for opening echo file
};

DEFINE_DEV(Dev_StdIO, "Standard IO", 1, Dev_Cmds, RDC_MAX, 0);

