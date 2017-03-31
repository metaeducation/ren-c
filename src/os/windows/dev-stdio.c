//
//  File: %dev-stdio.c
//  Summary: "Device: Standard I/O for Win32"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Provides basic I/O streams support for redirection and
// opening a console window if necessary.
//

#include <stdio.h>
#include <windows.h>
#include <process.h>
#include <assert.h>

#include <fcntl.h>
#include <io.h>

#include "reb-host.h"

#define BUF_SIZE (16 * 1024)    // MS restrictions apply

#define SF_DEV_NULL 31          // Local flag to mark NULL device.

#define CONSOLE_MODES \
        ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT \
        | 0x0040 | 0x0020       // quick edit and insert mode (not defined in VC6)

static HANDLE Std_Out = NULL;
static HANDLE Std_Inp = NULL;
static HANDLE Std_Echo = NULL;
static wchar_t *Std_Buf = NULL; // Used for UTF-8 conversion of stdin/stdout.

static BOOL Redir_Out = 0;
static BOOL Redir_Inp = 0;

extern i32 Request_Size_Rebreq(REBREQ *);

//**********************************************************************


static void Close_Stdio(void)
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


//
//  Quit_IO: C
//
DEVICE_CMD Quit_IO(REBREQ *dr)
{
    REBDEV *dev = (REBDEV*)dr; // just to keep compiler happy above

    Close_Stdio();
    //if (GET_FLAG(dev->flags, RDF_OPEN)) FreeConsole();
    CLR_FLAG(dev->flags, RDF_OPEN);
    return DR_DONE;
}


//
//  Open_IO: C
//
DEVICE_CMD Open_IO(REBREQ *req)
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

        if (!Redir_Inp || !Redir_Out) {
            // If either input or output is not redirected, preallocate
            // a buffer for conversion from/to UTF-8.
            Std_Buf = OS_ALLOC_N(wchar_t, BUF_SIZE);
        }

        if (!Redir_Inp) {
            // Make the Win32 console a bit smarter by default.
            SetConsoleMode(Std_Inp, CONSOLE_MODES);
        }
    }
    else
        SET_FLAG(dev->flags, SF_DEV_NULL);

    SET_FLAG(req->flags, RRF_OPEN);
    SET_FLAG(dev->flags, RDF_OPEN);

    return DR_DONE;
}


//
//  Close_IO: C
//
DEVICE_CMD Close_IO(REBREQ *req)
{
    REBDEV *dev = Devices[req->device];

    Close_Stdio();

    CLR_FLAG(dev->flags, RRF_OPEN);

    return DR_DONE;
}


//
//  Write_IO: C
//
// Low level "raw" standard output function.
//
// Allowed to restrict the write to a max OS buffer size.
//
// Returns the number of chars written.
//
DEVICE_CMD Write_IO(REBREQ *req)
{
    DWORD len;
    DWORD total = 0;
    BOOL ok = FALSE;

    if (GET_FLAG(req->modes, RDM_NULL)) {
        req->actual = req->length;
        return DR_DONE;
    }

    if (Std_Out) {

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
        //  FLUSH();
        //}
    }

    if (Std_Echo) { // always UTF-8
        WriteFile(Std_Echo, req->common.data, req->length, &total, 0);
        //FlushFileBuffers(Std_Echo);
    }

    return DR_DONE;
}


//
//  Read_IO: C
//
// Low level "raw" standard input function.
//
// The request buffer must be long enough to hold result.
//
// Result is NOT terminated (the actual field has length.)
//
DEVICE_CMD Read_IO(REBREQ *req)
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


//
//  Open_Echo: C
//
// Open a file for low-level console echo (output).
//
DEVICE_CMD Open_Echo(REBREQ *req)
{
    if (Std_Echo) {
        CloseHandle(Std_Echo);
        Std_Echo = 0;
    }

    struct devreq_file *file = DEVREQ_ECHO_FILE(req);

    if (file->path) {
        Std_Echo = CreateFile(file->path, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, CREATE_ALWAYS, 0, 0);
        if (Std_Echo == INVALID_HANDLE_VALUE) {
            Std_Echo = 0;
            req->error = GetLastError();
            return DR_ERROR;
        }
    }

    return DR_DONE;
}


//
//  Request_Size_IO: C
//
static i32 Request_Size_IO(REBREQ *req)
{
    (void)req; //unused
    return sizeof(struct devreq_file);
}

/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] =
{
    Request_Size_IO,
    0,  // init
    Quit_IO,
    Open_IO,
    Close_IO,
    Read_IO,
    Write_IO,
    0,  // poll
    0,  // connect
    0,  // query
    0,  // modify
    Open_Echo,  // CREATE used for opening echo file
};

DEFINE_DEV(Dev_StdIO, "Standard IO", 1, Dev_Cmds, RDC_MAX);

