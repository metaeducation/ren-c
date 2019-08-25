//
//  File: %dev-serial.c
//  Summary: "Device: Serial port access for Posix"
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
//=////////////////////////////////////////////////////////////////////////=//
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <termios.h>

#include "sys-core.h"

#include "req-serial.h"

#define MAX_SERIAL_PATH 128

/* BXXX constants are defined in termios.h */
const int speeds[] = {
    50, B50,
    75, B75,
    110, B110,
    134, B134,
    150, B150,
    200, B200,
    300, B300,
    600, B600,
    1200, B1200,
    1800, B1800,
    2400, B2400,
    4800, B4800,
    9600, B9600,
    19200, B19200,
    38400, B38400,
    57600, B57600,
    115200, B115200,
    230400, B230400,
    0
};

/***********************************************************************
**
**  Local Functions
**
***********************************************************************/

static struct termios *Get_Serial_Settings(int ttyfd)
{
    struct termios *attr
        = cast(struct termios*, malloc(sizeof(struct termios)));
    if (attr) {
        if (tcgetattr(ttyfd, attr) == -1) {
            free(attr);
            attr = NULL;
        }
    }
    return attr;
}


static REBINT Set_Serial_Settings(int ttyfd, REBREQ *req)
{
    REBINT n;
    struct termios attr;
    struct devreq_serial *serial = ReqSerial(req);
    REBINT speed = serial->baud;
    CLEARS(&attr);
#ifdef DEBUG_SERIAL
    printf("setting attributes: speed %d\n", speed);
#endif
    for (n = 0; speeds[n]; n += 2) {
        if (speed == speeds[n]) {
            speed = speeds[n+1];
            break;
        }
    }
    if (speeds[n] == 0) speed = B115200; // invalid, use default

    cfsetospeed (&attr, speed);
    cfsetispeed (&attr, speed);

    // TTY has many attributes. Refer to "man tcgetattr" for descriptions.
    // C-flags - control modes:
    attr.c_cflag |= CREAD | CLOCAL;

    attr.c_cflag &= ~CSIZE; /* clear data size bits */

    switch (serial->data_bits) {
        case 5:
            attr.c_cflag |= CS5;
            break;
        case 6:
            attr.c_cflag |= CS6;
            break;
        case 7:
            attr.c_cflag |= CS7;
            break;
        case 8:
        default:
            attr.c_cflag |= CS8;
    }

    switch (serial->parity) {
        case SERIAL_PARITY_ODD:
            attr.c_cflag |= PARENB;
            attr.c_cflag |= PARODD;
            break;

        case SERIAL_PARITY_EVEN:
            attr.c_cflag |= PARENB;
            attr.c_cflag &= ~PARODD;
            break;

        case SERIAL_PARITY_NONE:
        default:
            attr.c_cflag &= ~PARENB;
            break;
    }

    switch (serial->stop_bits) {
        case 2:
            attr.c_cflag |= CSTOPB;
            break;
        case 1:
        default:
            attr.c_cflag &= ~CSTOPB;
            break;
    }

#ifdef CNEW_RTSCTS
    switch (serial->parity) {
        case SERIAL_FLOW_CONTROL_HARDWARE:
            attr.c_cflag |= CNEW_RTSCTS;
            break;
        case SERIAL_FLOW_CONTROL_SOFTWARE:
            attr.c_cflag &= ~CNEW_RTSCTS;
            break;
        case SERIAL_FLOW_CONTROL_NONE:
        default:
            break;
    }
#endif

    // L-flags - local modes:
    attr.c_lflag = 0; // raw, not ICANON

    // I-flags - input modes:
    attr.c_iflag |= IGNPAR;

    // O-flags - output modes:
    attr.c_oflag = 0;

    // Control characters:
    // R3 devices are non-blocking (polled for changes):
    attr.c_cc[VMIN]  = 0;
    attr.c_cc[VTIME] = 0;

    // Make sure OS queues are empty:
    tcflush(ttyfd, TCIFLUSH);

    // Set new attributes:
    if (tcsetattr(ttyfd, TCSANOW, &attr)) return 2;

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
    struct devreq_serial *serial = ReqSerial(req);

    assert(serial->path != NULL);

    char path_utf8[MAX_SERIAL_PATH];
    REBLEN size = rebSpellIntoQ(
        path_utf8,
        MAX_SERIAL_PATH,
        serial->path,
        rebEND
    );

    if (path_utf8[0] != '/') { // relative path, insert `/dev` before slash
        memmove(path_utf8 + 4, path_utf8, size + 1);
        memcpy(path_utf8, "/dev", 4);
    }

    int h = open(path_utf8, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (h < 0)
        rebFail_OS (errno);

    // Getting prior attributes:

    serial->prior_attr = Get_Serial_Settings(h);
    if (tcgetattr(h, cast(struct termios*, serial->prior_attr)) != 0) {
        int errno_cache = errno;
        close(h);
        rebFail_OS (errno_cache);
    }

    if (Set_Serial_Settings(h, req) == 0) {
        int errno_cache = errno;
        close(h);
        rebFail_OS (errno_cache);
    }

    Req(req)->requestee.id = h;
    return DR_DONE;
}


//
//  Close_Serial: C
//
DEVICE_CMD Close_Serial(REBREQ *serial)
{
    struct rebol_devreq *req = Req(serial);
    if (req->requestee.id) {
        // !!! should we free serial->prior_attr termios struct?
        tcsetattr(
            req->requestee.id,
            TCSANOW,
            cast(struct termios*, ReqSerial(serial)->prior_attr)
        );
        close(req->requestee.id);
        req->requestee.id = 0;
    }
    return DR_DONE;
}


//
//  Read_Serial: C
//
DEVICE_CMD Read_Serial(REBREQ *serial)
{
    struct rebol_devreq *req = Req(serial);

    assert(req->requestee.id != 0);

    ssize_t result = read(req->requestee.id, req->common.data, req->length);

#ifdef DEBUG_SERIAL
    printf("read %d ret: %d\n", req->length, result);
#endif

    if (result < 0)
        rebFail_OS (errno);

    if (result == 0)
        return DR_PEND;

    req->actual = result;

    rebElide(
        "insert/only system/ports/system make event! [",
            "type: 'read",
            "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(serial))),
        "]",
    rebEND);

    return DR_DONE;
}


//
//  Write_Serial: C
//
DEVICE_CMD Write_Serial(REBREQ *serial)
{
    struct rebol_devreq *req = Req(serial);

    size_t len = req->length - req->actual;

    assert(req->requestee.id != 0);

    if (len <= 0)
        return DR_DONE;

    int result = write(req->requestee.id, req->common.data, len);

#ifdef DEBUG_SERIAL
    printf("write %d ret: %d\n", len, result);
#endif

    if (result < 0) {
        if (errno == EAGAIN)
            return DR_PEND;

        rebFail_OS (errno);
    }

    req->actual += result;
    req->common.data += result;
    if (req->actual >= req->length) {
        rebElide(
            "insert/only system/ports/system make event! [",
                "type: 'wrote",
                "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(serial))),
            "]",
        rebEND);

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

    if (req->requestee.id) {
        pfd.fd = req->requestee.id;
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

static DEVICE_CMD_CFUNC Dev_Cmds[RDC_MAX] = {
    0,
    0,
    Open_Serial,
    Close_Serial,
    Read_Serial,
    Write_Serial,
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
