//
//  File: %dev-stdio.c
//  Summary: "Device: Standard I/O for Posix"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Provides basic I/O streams support for redirection and
// opening a console window if necessary.
//

// !!! Read_IO writes directly into a BINARY!, whose size it needs to keep up
// to date (in order to have it properly terminated and please the GC).  At
// the moment it does this with the internal API, though libRebol should
// hopefully suffice in the future.  This is part of an ongoing effort to
// make the device layer work more in the vocabulary of Rebol types.
//
#include "sys-core.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "readline.h"


// Temporary globals: (either move or remove?!)
static int Std_Inp = STDIN_FILENO;
static int Std_Out = STDOUT_FILENO;

#if defined(REBOL_SMART_CONSOLE)
    extern STD_TERM *Term_IO;
#endif


static void Close_Stdio(void)
{
  #if defined(REBOL_SMART_CONSOLE)
    if (Term_IO) {
        Quit_Terminal(Term_IO);
        Term_IO = nullptr;
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
DEVICE_CMD Open_IO(REBREQ *io)
{
    struct rebol_devreq *req = Req(io);
    REBDEV *dev = req->device;

    // Avoid opening the console twice (compare dev and req flags):
    if (dev->flags & RDF_OPEN) {
        // Device was opened earlier as null, so req must have that flag:
        if (dev->flags & SF_DEV_NULL)
            req->modes |= RDM_NULL;
        req->flags |= RRF_OPEN;
        return DR_DONE; // Do not do it again
    }

    if (not (req->modes & RDM_NULL)) {

      #if defined(REBOL_SMART_CONSOLE)
        if (isatty(Std_Inp))  // is termios-capable (not redirected to a file)
            Term_IO = Init_Terminal();
      #endif
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
    REBDEV *dev = Req(req)->device;

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
DEVICE_CMD Write_IO(REBREQ *io)
{
    struct rebol_devreq *req = Req(io);

    if (req->modes & RDM_NULL) {
        req->actual = req->length;
        return DR_DONE;
    }

    if (Std_Out >= 0) {
      #if defined(REBOL_SMART_CONSOLE)
        if (Term_IO) {
            //
            // We need to sync the cursor position with writes.  This means
            // being UTF-8 aware, so the buffer we get has to be valid UTF-8
            // when written to a terminal for stdio.  (Arbitrary bytes of data
            // can be written when output is directed to cgi, but Term_IO
            // would be null.)
            //
            // !!! Longer term, the currency of exchange wouldn't be byte
            // buffers, but REBVAL*, in which case the UTF-8 nature of a TEXT!
            // would be assured, and we wouldn't be wasting this creation
            // of a new text and validating the UTF-8 *again*.
            //
            REBVAL *text = rebSizedText(
                cs_cast(req->common.data),
                req->length
            );
            Term_Insert(Term_IO, text);
            rebRelease(text);
        }
        else
      #endif
        {
            long total = write(Std_Out, req->common.data, req->length);

            if (total < 0)
                rebFail_OS(errno);

            assert(cast(size_t, total) == req->length);
        }
        req->actual = req->length;
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
DEVICE_CMD Read_IO(REBREQ *io)
{
    struct rebol_devreq *req = Req(io);

    // !!! While transitioning away from the R3-Alpha "abstract OS" model,
    // this hook now receives a BINARY! in req->text which it is expected to
    // fill with UTF-8 data, with req->length bytes.
    //
    REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(req->common.binary);
    REBLEN orig_len = VAL_LEN_AT(req->common.binary);
    assert(SER_AVAIL(bin) >= req->length);

    // Null redirection (should be handled at PORT! level to not ask for read)
    //
    assert(not (req->modes & RDM_NULL));

  #if defined(REBOL_SMART_CONSOLE)
    assert(not Term_IO);  // should have handled in %p-stdio.h
  #endif

    req->actual = 0;

    // read restarts on signal
    //
    long total = read(Std_Inp, BIN_AT(bin, orig_len), req->length);
    if (total < 0)
        rebFail_OS (errno);
    TERM_BIN_LEN(bin, orig_len + total);

    return DR_DONE;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_CFUNC Dev_Cmds[RDC_MAX] =
{
    0,  // init
    Quit_IO,
    Open_IO,
    Close_IO,
    Read_IO,
    Write_IO,
    0,  // connect
    0,  // query
    0,  // CREATE previously used for opening echo file
};

DEFINE_DEV(
    Dev_StdIO,
    "Standard IO", 1, Dev_Cmds, RDC_MAX, sizeof(struct rebol_devreq)
);
