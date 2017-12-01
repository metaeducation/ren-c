//
//  File: %dev-stdio.c
//  Summary: "Device: Standard I/O for Posix"
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
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "reb-host.h"

// Temporary globals: (either move or remove?!)
static int Std_Inp = STDIN_FILENO;
static int Std_Out = STDOUT_FILENO;

#ifndef HAS_SMART_CONSOLE   // console line-editing and recall needed
typedef struct term_data {
    char *buffer;
    char *residue;
    char *out;
    int pos;
    int end;
    int hist;
} STD_TERM;

extern STD_TERM *Init_Terminal();
extern void Quit_Terminal(STD_TERM*);
extern int Read_Line(STD_TERM*, REBYTE*, int);

STD_TERM *Term_IO;
#endif


static void Close_Stdio(void)
{
#ifndef HAS_SMART_CONSOLE
    if (Term_IO) {
        Quit_Terminal(Term_IO);
        Term_IO = 0;
    }
#endif
}


//
//  Quit_IO: C
//
DEVICE_CMD Quit_IO(REBREQ *dr)
{
    REBDEV *dev = (REBDEV*)dr; // just to keep compiler happy above

    Close_Stdio();

    dev->flags &= ~RDF_OPEN;
    return DR_DONE;
}


//
//  Open_IO: C
//
DEVICE_CMD Open_IO(REBREQ *req)
{
    REBDEV *dev = Devices[req->device];

    // Avoid opening the console twice (compare dev and req flags):
    if (dev->flags & RDF_OPEN) {
        // Device was opened earlier as null, so req must have that flag:
        if (dev->flags & SF_DEV_NULL)
            req->modes |= RDM_NULL;
        req->flags |= RRF_OPEN;
        return DR_DONE; // Do not do it again
    }

    if (NOT(req->modes & RDM_NULL)) {

#ifndef HAS_SMART_CONSOLE
        if (isatty(Std_Inp))
            Term_IO = Init_Terminal();
#endif
        //printf("%x\r\n", req->requestee.handle);
    }
    else
        dev->flags |= SF_DEV_NULL;

    req->flags |= RRF_OPEN;
    dev->flags |= RDF_OPEN;

    return DR_DONE;
}


//
//  Close_IO: C
//
DEVICE_CMD Close_IO(REBREQ *req)
{
    REBDEV *dev = Devices[req->device];

    Close_Stdio();

    dev->flags &= ~RRF_OPEN;

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
    long total;

    if (req->modes & RDM_NULL) {
        req->actual = req->length;
        return DR_DONE;
    }

    if (Std_Out >= 0) {

        total = write(Std_Out, req->common.data, req->length);

        if (total < 0)
            rebFail_OS(errno);

        //if (req->flags & RRF_FLUSH) {
            //FLUSH();
        //}

        req->actual = total;
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
    long total = 0;
    int len = req->length;

    if (req->modes & RDM_NULL) {
        req->common.data[0] = 0;
        return DR_DONE;
    }

    req->actual = 0;

    if (Std_Inp >= 0) {

        // Perform a processed read or a raw read?
#ifndef HAS_SMART_CONSOLE
        if (Term_IO)
            total = Read_Line(Term_IO, req->common.data, len);
        else
#endif
            total = read(Std_Inp, req->common.data, len); /* will be restarted in case of signal */

        if (total < 0)
            rebFail_OS (errno);

        req->actual = total;
    }

    return DR_DONE;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] =
{
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
    0,  // CREATE previously used for opening echo file
};

DEFINE_DEV(
    Dev_StdIO,
    "Standard IO", 1, Dev_Cmds, RDC_MAX, sizeof(struct devreq_file)
);
