//
//  File: %dev-signal.c
//  Summary: "Device: Signal access on Linux"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2017 Rebol Open Source Contributors
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
// Provides a very simple interface to the signals on Linux
//

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <sys/signal.h>

#include "sys-core.h"

#include "signal-req.h"

//
//  Open_Signal: C
//
DEVICE_CMD Open_Signal(REBREQ *signal)
{
    struct rebol_devreq *req = Req(signal);

  #ifdef CHECK_MASK_OVERLAP //doesn't work yet
    sigset_t mask;
    if (sigprocmask(SIG_BLOCK, NULL, &mask) < 0)
        rebFail_OS (errno);

    sigset_t overlap;
    if (sigandset(&overlap, &mask, &ReqPosixSignal(signal)->mask) < 0)
        rebFail_OS (errno);

    if (!sigisemptyset(&overlap))
        rebFail_OS (EBUSY);
  #endif

    if (sigprocmask(SIG_BLOCK, &ReqPosixSignal(signal)->mask, NULL) < 0)
        rebFail_OS (errno);

    req->flags |= RRF_OPEN;

    rebElide(
        "insert/only system/ports/system make event! [",
            "type: 'open",
            "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(signal))),
        "]",
    rebEND);

    return DR_DONE;
}


//
//  Close_Signal: C
//
DEVICE_CMD Close_Signal(REBREQ *signal)
{
    struct rebol_devreq *req = Req(signal);
    if (sigprocmask(SIG_UNBLOCK, &ReqPosixSignal(signal)->mask, NULL) < 0)
        rebFail_OS (errno);

    req->flags &= ~RRF_OPEN;
    return DR_DONE;
}


//
//  Read_Signal: C
//
DEVICE_CMD Read_Signal(REBREQ *signal)
{
    struct rebol_devreq *req = Req(signal);

    struct timespec timeout = {0, 0};
    unsigned int i = 0;

    errno = 0;

    for (i = 0; i < req->length; i ++) {
        int result = sigtimedwait(
            &ReqPosixSignal(signal)->mask,
            &(cast(siginfo_t*, req->common.data)[i]),
            &timeout
        );

        if (result < 0) {
            if (errno != EAGAIN && i == 0)
                rebFail_OS (errno);
            break;
        }
    }

    req->actual = i;
    if (i <= 0)
        return DR_PEND;

    //printf("read %d signals\n", req->actual);

    rebElide(
        "insert/only system/ports/system make event! [",
            "type: 'read",
            "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(signal))),
        "]",
    rebEND);

    return DR_DONE;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_CFUNC Dev_Cmds[RDC_MAX] =
{
    0,
    0,
    Open_Signal,
    Close_Signal,
    Read_Signal,
    0,
    0,
};

DEFINE_DEV(Dev_Signal, "Signal", 1, Dev_Cmds, RDC_MAX, sizeof(struct rebol_devreq));
