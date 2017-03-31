//
//  File: %dev-clipboard.c
//  Summary: "Device: Clipboard access for Win32"
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
// Provides a very simple interface to the clipboard for text.
// May be expanded in the future for images, etc.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! Unlike on Linux/Posix, the basic Win32 API is able to support
// a clipboard device in a non-graphical build without an added
// dependency.  For this reason, the Rebol core build included the
// clipboard device...which finds its way into a fixed-size table
// when it should be registered in a more dynamic and conditional way.
// Ren/C needs to improve the way that per-platform code can be
// included in a static build to not rely on this table the way
// hostkit does.
//

#include <stdio.h>

#include "reb-host.h"
#include "sys-net.h"

extern void Signal_Device(REBREQ *req, REBINT type);
extern i32 Request_Size_Rebreq(REBREQ *);

//
//  Open_Clipboard: C
//
DEVICE_CMD Open_Clipboard(REBREQ *req)
{
    SET_OPEN(req);
    return DR_DONE;
}


//
//  Close_Clipboard: C
//
DEVICE_CMD Close_Clipboard(REBREQ *req)
{
    SET_CLOSED(req);
    return DR_DONE;
}


//
//  Read_Clipboard: C
//
DEVICE_CMD Read_Clipboard(REBREQ *req)
{
    HANDLE data;
    wchar_t *cp;
    wchar_t *bin;
    REBINT len;

    req->actual = 0;

    // If there is no clipboard data:
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        req->error = 10;
        return DR_ERROR;
    }

    if (!OpenClipboard(NULL)) {
        req->error = 20;
        return DR_ERROR;
    }

    // Read the UTF-8 data:
    if ((data = GetClipboardData(CF_UNICODETEXT)) == NULL) {
        CloseClipboard();
        req->error = 30;
        return DR_ERROR;
    }

    cp = cast(wchar_t*, GlobalLock(data));
    if (!cp) {
        GlobalUnlock(data);
        CloseClipboard();
        req->error = 40;
        return DR_ERROR;
    }

    len = wcslen(cp);
    bin = OS_ALLOC_N(wchar_t, len + 1);
    wcsncpy(bin, cp, len);

    GlobalUnlock(data);

    CloseClipboard();

    SET_FLAG(req->flags, RRF_WIDE);
    req->common.data = cast(REBYTE *, bin);
    req->actual = len * sizeof(wchar_t);
    Signal_Device(req, EVT_READ);
    return DR_DONE;
}


//
//  Write_Clipboard: C
//
// Works for Unicode and ASCII strings.
// Length is number of bytes passed (not number of chars).
//
DEVICE_CMD Write_Clipboard(REBREQ *req)
{
    HANDLE data;
    REBYTE *bin;
    REBCNT err;
    REBINT len = req->length; // in bytes

    req->actual = 0;

    data = GlobalAlloc(GHND, len + 4);
    if (data == NULL) {
        req->error = 5;
        return DR_ERROR;
    }

    // Lock and copy the string:
    bin = cast(REBYTE*, GlobalLock(data));
    if (bin == NULL) {
        req->error = 10;
        return DR_ERROR;
    }

    memcpy(bin, req->common.data, len);
    bin[len] = 0;
    GlobalUnlock(data);

    if (!OpenClipboard(NULL)) {
        req->error = 20;
        return DR_ERROR;
    }

    EmptyClipboard();

    err = !SetClipboardData(GET_FLAG(req->flags, RRF_WIDE) ? CF_UNICODETEXT : CF_TEXT, data);

    CloseClipboard();

    if (err) {
        req->error = 50;
        return DR_ERROR;
    }

    req->actual = len;
    Signal_Device(req, EVT_WROTE);
    return DR_DONE;
}


//
//  Poll_Clipboard: C
//
DEVICE_CMD Poll_Clipboard(REBREQ *req)
{
    UNUSED(req);
    return DR_DONE;
}

/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] =
{
    Request_Size_Rebreq,
    0,
    0,
    Open_Clipboard,
    Close_Clipboard,
    Read_Clipboard,
    Write_Clipboard,
    Poll_Clipboard,
};

DEFINE_DEV(Dev_Clipboard, "Clipboard", 1, Dev_Cmds, RDC_MAX);
