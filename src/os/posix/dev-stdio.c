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
**  Title: Device: Standard I/O for Posix
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
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "reb-host.h"

#define SF_DEV_NULL 31      // local flag to mark NULL device

// Temporary globals: (either move or remove?!)
static int Std_Inp = STDIN_FILENO;
static int Std_Out = STDOUT_FILENO;
static FILE *Std_Echo = NULL;

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
extern int Read_Line(STD_TERM*, char*, int);

STD_TERM *Term_IO;
#endif

extern void Put_Str(const REBYTE *buf);

static int interrupted = 0;

static void Handle_Signal(int sig)
{
    RL_Escape(0); /* This will cause the next evalution escaped */
    interrupted = 1;
}

static void Init_Signals(void)
{
    signal(SIGINT, Handle_Signal);
    signal(SIGHUP, Handle_Signal);
    signal(SIGTERM, Handle_Signal);
}

static void Close_Stdio(void)
{
#ifndef HAS_SMART_CONSOLE
    if (Term_IO) {
        Quit_Terminal(Term_IO);
        Term_IO = 0;
    }
#endif
    if (Std_Echo) {
        fclose(Std_Echo);
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

    Init_Signals();

    if (!GET_FLAG(req->modes, RDM_NULL)) {

#ifndef HAS_SMART_CONSOLE
        if (isatty(Std_Inp))
            Term_IO = Init_Terminal();
#endif
        //printf("%x\r\n", req->requestee.handle);
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
    long total;

    if (GET_FLAG(req->modes, RDM_NULL)) {
        req->actual = req->length;
        return DR_DONE;
    }

    if (Std_Out >= 0) {

        total = write(Std_Out, req->common.data, req->length);

        if (total < 0) {
            req->error = errno;
            return DR_ERROR;
        }

        //if (GET_FLAG(req->flags, RRF_FLUSH)) {
            //FLUSH();
        //}

        req->actual = total;
    }

    if (Std_Echo) {
        fwrite(req->common.data, req->length, 1, Std_Echo);
        //fflush(Std_Echo); //slow!
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

    if (GET_FLAG(req->modes, RDM_NULL)) {
        req->common.data[0] = 0;
        return DR_DONE;
    }

    req->actual = 0;

    if (Std_Inp >= 0) {

        interrupted = 0;
        // Perform a processed read or a raw read?
#ifndef HAS_SMART_CONSOLE
        if (Term_IO)
            total = Read_Line(Term_IO, s_cast(req->common.data), len);
        else
#endif
            total = read(Std_Inp, req->common.data, len); /* will be restarted in case of signal */

        if (total < 0) {
            req->error = errno;
            return DR_ERROR;
        }
        if (interrupted) {
            char noop[] = "does[]\n";
            APPEND_BYTES_LIMIT(req->common.data, cb_cast(noop), len);
            total += sizeof(noop);
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
        fclose(Std_Echo);
        Std_Echo = 0;
    }

    if (req->special.file.path) {
        Std_Echo = fopen(req->special.file.path, "w");  // null on error
        if (!Std_Echo) {
            req->error = errno;
            return DR_ERROR;
        }
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
    Open_Echo,  // CREATE used for opening echo file
};

DEFINE_DEV(Dev_StdIO, "Standard IO", 1, Dev_Cmds, RDC_MAX, 0);
