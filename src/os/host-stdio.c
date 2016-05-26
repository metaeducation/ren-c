//
//  File: %host-stdio.c
//  Summary: "Simple helper functions for host-side standard I/O"
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
// OS independent
//
// Interfaces to the stdio device for standard I/O on the host.
// All stdio within REBOL uses UTF-8 encoding so the functions
// shown here operate on UTF-8 bytes, regardless of the OS.
// The conversion to wide-chars for OSes like Win32 is done in
// the StdIO Device code.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include <stdio.h>
#include <string.h>
#include "reb-host.h"

void Host_Crash(const char *reason);

// Temporary globals: (either move or remove?!)
REBREQ Std_IO_Req;
static REBYTE *inbuf;
static REBCNT inbuf_len = 32*1024;

static REBYTE *Get_Next_Line()
{
    REBYTE *bp;
    REBYTE *out;
    REBCNT len;

    // Scan for line terminator or end:
    for (bp = inbuf; *bp != CR && *bp != LF && *bp != 0; bp++);

    // If found, copy the line and remove it from buffer:
    if (*bp) {
        if (*bp == CR && bp[1] == LF) bp++;
        len = bp - inbuf;
        out = OS_ALLOC_N(REBYTE, len + 2);
        COPY_BYTES(out, inbuf, len+1);
        out[len+1] = 0;
        memmove(inbuf, bp + 1, 1 + LEN_BYTES(bp + 1));
        return out;
    }

    return 0; // more input needed
}

static REBOOL Fetch_Buf()
{
    REBCNT len = LEN_BYTES(inbuf);

    Std_IO_Req.common.data = inbuf + len;
    Std_IO_Req.length = inbuf_len - len - 1;
    Std_IO_Req.actual = 0;

    OS_Do_Device(&Std_IO_Req, RDC_READ);

    // If error, don't crash, just ignore it:
    if (Std_IO_Req.error) return FALSE; //Host_Crash("stdio read");

    // Terminate (LF) last line?
    if (len > 0 && Std_IO_Req.actual == 0) {
        inbuf[len++] = LF;
        inbuf[len] = 0;
        return TRUE;
    }

    // Null terminate buffer:
    len = Std_IO_Req.actual;
    Std_IO_Req.common.data[len] = 0;
    return LOGICAL(len > 0);
}


//
//  Open_StdIO: C
//
// Open REBOL's standard IO device. This same device is used
// by both the host code and the R3 DLL itself.
//
// This must be done before any other initialization is done
// in order to output banners or errors.
//
void Open_StdIO(void)
{
    CLEARS(&Std_IO_Req);
    Std_IO_Req.clen = sizeof(Std_IO_Req);
    Std_IO_Req.device = RDI_STDIO;

    OS_Do_Device(&Std_IO_Req, RDC_OPEN);

    if (Std_IO_Req.error) Host_Crash("stdio open");

    inbuf = OS_ALLOC_N(REBYTE, inbuf_len);
    inbuf[0] = 0;
}


//
//  Close_StdIO: C
//
// Complement to Open_StdIO()
//
void Close_StdIO(void)
{
    OS_FREE(inbuf);
}


//
//  Put_Str: C
//
// Outputs a null terminated UTF-8 string.
// If buf is larger than StdIO Device allows, error out.
// OS dependent line termination must be done prior to call.
//
void Put_Str(const REBYTE *buf)
{
    /* This function could be called by signal handler and inside of Fetch_Buf */
    REBREQ req;
    memcpy(&req, &Std_IO_Req, sizeof(req));

    // !!! A request should ideally have a way to enforce that it is not
    // going to modify the data.  For now we "trust it" and use m_cast.
    // Undefined behavior will result should a RDC_WRITE request make
    // modifications to the data pointed to.
    //
    req.common.data = m_cast(REBYTE*, buf);
    req.length = LEN_BYTES(buf);
    req.actual = 0;

    OS_Do_Device(&req, RDC_WRITE);

    if (req.error) Host_Crash("stdio write");
}
