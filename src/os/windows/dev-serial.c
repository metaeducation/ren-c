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

#include <windows.h>
#include <stdio.h>
#include <assert.h>

#include "reb-host.h"

extern void Signal_Device(REBREQ *req, REBINT type);

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


/***********************************************************************
**
**  Local Functions
**
***********************************************************************/
static REBINT Set_Serial_Settings(HANDLE h, struct devreq_serial *serial)
{
    DCB dcbSerialParams;
    REBINT n;
    int speed = serial->baud;

    memset(&dcbSerialParams, '\0', sizeof(dcbSerialParams));
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(h, &dcbSerialParams)) return 1;


    for (n = 0; speeds[n]; n += 2) {
        if (speed == speeds[n]) {
            dcbSerialParams.BaudRate = speeds[n+1];
            break;
        }
    }
    if (speeds[n] == 0) dcbSerialParams.BaudRate = CBR_115200; // invalid, use default

    dcbSerialParams.ByteSize = serial->data_bits;
    dcbSerialParams.StopBits = serial->stop_bits == 1? ONESTOPBIT : TWOSTOPBITS;
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


    if(!SetCommState(h, &dcbSerialParams)) {
        return 1;
    }

    PurgeComm(h,PURGE_RXCLEAR|PURGE_TXCLEAR);  //make sure buffers are clean
    return 0;
}

//
//  Open_Serial: C
//
// serial.path = the /dev name for the serial port
// serial.baud = speed (baudrate)
//
DEVICE_CMD Open_Serial(REBREQ *req)
{
    HANDLE h;
    COMMTIMEOUTS timeouts; //add in timeouts? Currently unused
    struct devreq_serial *serial = DEVREQ_SERIAL(req);

    memset(&timeouts, '\0', sizeof(timeouts));

    // req->special.serial.path should be prefixed with "\\.\" to allow for higher com port numbers
    wchar_t fullpath[MAX_SERIAL_DEV_PATH] = L"\\\\.\\";

    if (!serial->path) {
        req->error = -RFE_BAD_PATH;
        return DR_ERROR;
    }

    wcsncat(fullpath, serial->path, MAX_SERIAL_DEV_PATH);

    h = CreateFile(fullpath, GENERIC_READ|GENERIC_WRITE, 0, NULL,OPEN_EXISTING, 0, NULL );
    if (h == INVALID_HANDLE_VALUE) {
        req->error = -RFE_OPEN_FAIL;
        return DR_ERROR;
    }

    if (Set_Serial_Settings(h, serial)) {
        CloseHandle(h);
        req->error = -RFE_OPEN_FAIL;
        return DR_ERROR;
    }


    // See: http://msdn.microsoft.com/en-us/library/windows/desktop/aa363190%28v=vs.85%29.aspx
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 1;   // These two write lines may need to be set to 0.
    timeouts.WriteTotalTimeoutConstant = 1;
    if (!SetCommTimeouts(h, &timeouts)) {
        CloseHandle(h);
        req->error = -RFE_OPEN_FAIL;
        return DR_ERROR;
    }

    req->requestee.handle = h;
    return DR_DONE;
}


//
//  Close_Serial: C
//
DEVICE_CMD Close_Serial(REBREQ *req)
{
    if (req->requestee.handle) {
        // !!! Should we free req->special.serial.prior_attr termios struct?
        CloseHandle(req->requestee.handle);
        req->requestee.handle = 0;
    }
    return DR_DONE;
}


//
//  Read_Serial: C
//
DEVICE_CMD Read_Serial(REBREQ *req)
{
    DWORD result = 0;
    if (!req->requestee.handle) {
        req->error = -RFE_NO_HANDLE;
        return DR_ERROR;
    }

    //printf("reading %d bytes\n", req->length);
    if (!ReadFile(req->requestee.handle, req->common.data, req->length, &result, 0)) {
        req->error = -RFE_BAD_READ;
        Signal_Device(req, EVT_ERROR);
        return DR_ERROR;
    } else {
        if (result == 0) {
            return DR_PEND;
        } else if (result > 0){
            //printf("read %d bytes\n", req->actual);
            req->actual = result;
            Signal_Device(req, EVT_READ);
        }
    }

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
    DWORD result = 0;
    DWORD len = req->length - req->actual;
    if (!req->requestee.handle) {
        req->error = -RFE_NO_HANDLE;
        return DR_ERROR;
    }

    if (len <= 0) return DR_DONE;

    if (!WriteFile(
        req->requestee.handle, req->common.data, len, &result, NULL
    )) {
        req->error = -RFE_BAD_WRITE;
        Signal_Device(req, EVT_ERROR);
        return DR_ERROR;
    }

#ifdef DEBUG_SERIAL
    printf("write %d ret: %d\n", req->length, req->actual);
#endif

    req->actual += result;
    req->common.data += result;
    if (req->actual >= req->length) {
        Signal_Device(req, EVT_WROTE);
        return DR_DONE;
    } else {
        SET_FLAG(req->flags, RRF_ACTIVE); /* notify OS_WAIT of activity */
        return DR_PEND;
    }
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

