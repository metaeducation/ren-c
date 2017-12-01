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

#include <windows.h>

#include "reb-host.h"


//
//  Open_Clipboard: C
//
DEVICE_CMD Open_Clipboard(REBREQ *req)
{
    req->flags |= RRF_OPEN;
    return DR_DONE;
}


//
//  Close_Clipboard: C
//
DEVICE_CMD Close_Clipboard(REBREQ *req)
{
    req->flags &= ~RRF_OPEN;
    return DR_DONE;
}


//
//  Read_Clipboard: C
//
DEVICE_CMD Read_Clipboard(REBREQ *req)
{
    req->actual = 0;

    SetLastError(NO_ERROR);
    if (NOT(IsClipboardFormatAvailable(CF_UNICODETEXT))) {
        //
        // This is not necessarily an "error", it just may be the clipboard
        // doesn't have text on it (an image, or maybe nothing at all);
        //
        DWORD last_error = GetLastError();
        if (last_error != NO_ERROR)
            rebFail_OS (last_error);

        req->common.data = cast(REBYTE*, rebBlank());
        req->actual = 0; // !!! not needed (REBVAL* knows its size)
        return DR_DONE;
    }

    if (NOT(OpenClipboard(NULL)))
        rebFail ("OpenClipboard() failed while reading");

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h == NULL) {
        CloseClipboard();
        rebFail ("IsClipboardFormatAvailable()/GetClipboardData() mismatch");
    }

    wchar_t *wide = cast(wchar_t*, GlobalLock(h));
    if (wide == NULL) {
        CloseClipboard();
        rebFail ("Couldn't GlobalLock() UTF-16 clipboard data");
    }

    REBVAL *str = rebStringW(wide);

    GlobalUnlock(h);
    CloseClipboard();

    // !!! We got wide character data back, which had to be made into a
    // string.  But READ wants BINARY! data.  With UTF-8 Everywhere, the
    // underlying byte representation of the string could be locked andaliased
    // as a UTF-8 binary series.  But a conversion is needed for the moment.

    size_t size;
    char *utf8 = rebSpellingOfAlloc(&size, str);
    rebRelease(str);

    REBVAL *binary = rebBinary(utf8, size);
    OS_FREE(utf8);

    // !!! The REBREQ and Device model is being gutted and replaced.  Formerly
    // this would return OS_ALLOC()'d wide character data and set a RRF_WIDE
    // flag to indicate that, now we slip a REBVAL* in.
    //
    req->common.data = cast(REBYTE*, binary); // !!! Hack
    req->actual = 0; // !!! not needed (REBVAL* knows its size)
    OS_SIGNAL_DEVICE(req, EVT_READ);
    return DR_DONE;
}


//
//  Write_Clipboard: C
//
// Length is number of bytes passed (not number of chars).
//
DEVICE_CMD Write_Clipboard(REBREQ *req)
{
    // !!! Traditionally the currency of READ and WRITE is binary data.
    // This intermediate stage hacks that up a bit by having the port send
    // string data, in which the LEN makes sense.  This should be reviewed,
    // but since to the user it appears compatible with R3-Alpha behavior it
    // is kept as is.
    //
    REBVAL *str = cast(REBVAL*, req->common.data);
    assert(RXT_STRING == rebTypeOf(str));

    REBCNT len = req->length; // may only want /PART of the string to write

    req->actual = 0;

    if (NOT(OpenClipboard(NULL)))
        rebFail ("OpenClipboard() failed during write to clipboard");

    if (NOT(EmptyClipboard())) // !!! is this superfluous?
        rebFail ("EmptyClipboard() failed during write to clipboard");

    // Clipboard wants a Windows memory handle with wchar_t UTF-16 data.  So
    // allocate sufficienctly sized handle, decode Rebol STRING! into it, and
    // transfer ownership of that handle to the clipboard.

    HANDLE h = GlobalAlloc(GHND, sizeof(wchar_t) * (len + 1));
    if (h == NULL) // per documentation, not INVALID_HANDLE_VALUE
        rebFail ("GlobalAlloc() failed during write to clipboard");

    wchar_t *wide = cast(wchar_t*, GlobalLock(h));
    if (wide == NULL)
        rebFail ("GlobalLock() failed during write to clipboard");

    REBCNT len_check = rebSpellingOfW(wide, len, str); // UTF-16 extraction
    assert(len <= len_check); // may only be writing /PART of the string
    UNUSED(len_check);

    GlobalUnlock(h);

    HANDLE h_check = SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();

    if (h_check == NULL)
        rebFail ("SetClipboardData() failed.");

    assert(h_check == h);

    req->actual = len; // !!! Pointless... str is released by ON_WAKE_UP
    OS_SIGNAL_DEVICE(req, EVT_WROTE);
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
    0,
    0,
    Open_Clipboard,
    Close_Clipboard,
    Read_Clipboard,
    Write_Clipboard,
    Poll_Clipboard,
};

DEFINE_DEV(Dev_Clipboard, "Clipboard", 1, Dev_Cmds, RDC_MAX, sizeof(REBREQ));
