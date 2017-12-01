//
//  File: %dev-serial.c
//  Summary: "Device: Serial port access for Windows"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2013 REBOL Technologies
// Copyright 2013-2017 Rebol Open Source Contributors
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
// !!! The serial port code was derived from code originally by Carl
// Sassenrath and used for home automation:
//
// https://www.youtube.com/watch?v=Axus6jF6YOQ
//
// It was added to R3-Alpha by Joshua Shireman, and incorporated into the
// Ren-C branch when it was launched.  Due to the fact that few developers
// have serial interfaces on their current machines (or serial devices to
// use them with), it has had limited testing--despite needing continuous
// modification to stay in sync with core changes.
//
// (At one point it was known to be broken due to variances in handling of
// character widths on Windows vs. UNIX, but the "UTF-8 Everywhere" initiative
// should help with that.)
//

#include <windows.h>
#include <stdio.h>
#include <assert.h>

#include "reb-host.h"

#define MAX_SERIAL_DEV_PATH 128

const int speeds[] = {
    110, CBR_110,
    300, CBR_300,
    600, CBR_600,
    1200, CBR_1200,
    2400, CBR_2400,
    4800, CBR_4800,
    9600, CBR_9600,
    14400, CBR_14400,
    19200, CBR_19200,
    38400, CBR_38400,
    57600, CBR_57600,
    115200, CBR_115200,
    128000, CBR_128000,
    230400, CBR_256000,
    0
};


//
//  Open_Serial: C
//
// serial.path = the /dev name for the serial port
// serial.baud = speed (baudrate)
//
DEVICE_CMD Open_Serial(REBREQ *req)
{
    struct devreq_serial *serial = DEVREQ_SERIAL(req);

    // req->special.serial.path should be prefixed with "\\.\" to allow for
    // higher com port numbers
    //
    wchar_t fullpath[MAX_SERIAL_DEV_PATH] = L"\\\\.\\";

    assert(serial->path != NULL);

    // Concatenate the "spelling" of the serial port request by asking it
    // to be placed at the end of the buffer.
    //
    REBCNT buf_left = MAX_SERIAL_DEV_PATH - wcslen(fullpath) - 1;
    REBCNT chars_appended = rebSpellingOfW(
        &fullpath[wcslen(fullpath)],
        buf_left, // space, minus terminator
        serial->path
    );
    if (chars_appended > buf_left)
        rebFail ("Serial file path too long for MAX_SERIAL_DEV_PATH");

    HANDLE h = CreateFile(
        fullpath,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    if (h == INVALID_HANDLE_VALUE)
        rebFail_OS (GetLastError());

    DCB dcbSerialParams;
    memset(&dcbSerialParams, '\0', sizeof(dcbSerialParams));
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (NOT(GetCommState(h, &dcbSerialParams))) {
        CloseHandle(h);
        rebFail_OS (GetLastError());
    }

    int speed = serial->baud;

    REBINT n;
    for (n = 0; speeds[n]; n += 2) {
        if (speed == speeds[n]) {
            dcbSerialParams.BaudRate = speeds[n+1];
            break;
        }
    }

    if (speeds[n] == 0) // invalid, use default
        dcbSerialParams.BaudRate = CBR_115200;

    dcbSerialParams.ByteSize = serial->data_bits;
    if (serial->stop_bits == 1)
        dcbSerialParams.StopBits = ONESTOPBIT;
    else
        dcbSerialParams.StopBits = TWOSTOPBITS;

    switch (serial->parity) {
    case SERIAL_PARITY_ODD:
        dcbSerialParams.Parity = ODDPARITY;
        break;

    case SERIAL_PARITY_EVEN:
        dcbSerialParams.Parity = EVENPARITY;
        break;

    case SERIAL_PARITY_NONE:
    default:
        dcbSerialParams.Parity = NOPARITY;
        break;
    }

    if (NOT(SetCommState(h, &dcbSerialParams))) {
        CloseHandle(h);
        rebFail_OS (GetLastError());
    }

    // Make sure buffers are clean

    if (NOT(PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR))) {
        CloseHandle(h);
        rebFail_OS (GetLastError());
    }

    // !!! Comment said "add in timeouts? currently unused".  This might
    // suggest a question of whether the request itself have some way of
    // asking for custom timeouts, while the initialization of the timeouts
    // below is the same for every request.
    //
    // http://msdn.microsoft.com/en-us/library/windows/desktop/aa363190%28v=vs.85%29.aspx
    //
    COMMTIMEOUTS timeouts;
    memset(&timeouts, '\0', sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 1; // !!! should this be 0?
    timeouts.WriteTotalTimeoutConstant = 1; // !!! should this be 0?

    if (NOT(SetCommTimeouts(h, &timeouts))) {
        CloseHandle(h);
        rebFail_OS (GetLastError());
    }

    req->requestee.handle = h;
    return DR_DONE;
}


//
//  Close_Serial: C
//
DEVICE_CMD Close_Serial(REBREQ *req)
{
    if (req->requestee.handle != NULL) {
        //
        // !!! Should we free req->special.serial.prior_attr termios struct?
        //
        CloseHandle(req->requestee.handle);
        req->requestee.handle = NULL;
    }
    return DR_DONE;
}


//
//  Read_Serial: C
//
DEVICE_CMD Read_Serial(REBREQ *req)
{
    assert(req->requestee.handle != NULL);

    //printf("reading %d bytes\n", req->length);

    DWORD result;
    if (NOT(ReadFile(
        req->requestee.handle, req->common.data, req->length, &result, 0
    ))){
        rebFail_OS (GetLastError());
    }

    if (result == 0)
        return DR_PEND;

    req->actual = result;
    OS_SIGNAL_DEVICE(req, EVT_READ);

#ifdef DEBUG_SERIAL
    printf("read %d ret: %d\n", req->length, req->actual);
#endif

    return DR_DONE;
}


//
//  Write_Serial: C
//
DEVICE_CMD Write_Serial(REBREQ *req)
{
    DWORD len = req->length - req->actual;

    assert(req->requestee.handle != NULL);

    if (len <= 0)
        return DR_DONE;

    DWORD result;
    if (NOT(WriteFile(
        req->requestee.handle, req->common.data, len, &result, NULL
    ))){
        rebFail_OS (GetLastError());
    }

#ifdef DEBUG_SERIAL
    printf("write %d ret: %d\n", req->length, req->actual);
#endif

    req->actual += result;
    req->common.data += result;
    if (req->actual >= req->length) {
        OS_SIGNAL_DEVICE(req, EVT_WROTE);
        return DR_DONE;
    }

    req->flags |= RRF_ACTIVE; // notify OS_WAIT of activity
    return DR_PEND;
}


//
//  Query_Serial: C
//
DEVICE_CMD Query_Serial(REBREQ *req)
{
#ifdef QUERY_IMPLEMENTED
    struct pollfd pfd;

    if (req->requestee.handle) {
        pfd.fd = req->requestee.handle;
        pfd.events = POLLIN;
        n = poll(&pfd, 1, 0);
    }
#else
    UNUSED(req);
#endif
    return DR_DONE;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] = {
    0,
    0,
    Open_Serial,
    Close_Serial,
    Read_Serial,
    Write_Serial,
    0,  // poll
    0,  // connect
    Query_Serial,
    0,  // modify
    0,  // create
    0,  // delete
    0   // rename
};

DEFINE_DEV(
    Dev_Serial,
    "Serial IO", 1, Dev_Cmds, RDC_MAX, sizeof(struct devreq_serial)
);
