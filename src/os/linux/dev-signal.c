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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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

#include "reb-host.h"


//
//  Open_Signal: C
//
DEVICE_CMD Open_Signal(REBREQ *req)
{
    struct devreq_posix_signal *signal = DEVREQ_POSIX_SIGNAL(req);

#ifdef CHECK_MASK_OVERLAP //doesn't work yet
    sigset_t mask;
    if (sigprocmask(SIG_BLOCK, NULL, &mask) < 0)
        rebFail_OS (errno);

    sigset_t overlap;
    if (sigandset(&overlap, &mask, &signal->mask) < 0)
        rebFail_OS (errno);

    if (!sigisemptyset(&overlap))
        rebFail_OS (EBUSY);
#endif

    if (sigprocmask(SIG_BLOCK, &signal->mask, NULL) < 0)
        rebFail_OS (errno);

    req->flags |= RRF_OPEN;
    OS_SIGNAL_DEVICE(req, EVT_OPEN);

    return DR_DONE;
}


//
//  Close_Signal: C
//
DEVICE_CMD Close_Signal(REBREQ *req)
{
    struct devreq_posix_signal *signal = DEVREQ_POSIX_SIGNAL(req);
    if (sigprocmask(SIG_UNBLOCK, &signal->mask, NULL) < 0)
        rebFail_OS (errno);

    req->flags &= ~RRF_OPEN;
    return DR_DONE;
}


//
//  Read_Signal: C
//
DEVICE_CMD Read_Signal(REBREQ *req)
{
    struct timespec timeout = {0, 0};
    unsigned int i = 0;

    struct devreq_posix_signal *signal = DEVREQ_POSIX_SIGNAL(req);
    errno = 0;

    for (i = 0; i < req->length; i ++) {
        int result = sigtimedwait(
            &signal->mask,
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
    OS_SIGNAL_DEVICE(req, EVT_READ);
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
    Open_Signal,
    Close_Signal,
    Read_Signal,
    0,
    0,
};

DEFINE_DEV(Dev_Signal, "Signal", 1, Dev_Cmds, RDC_MAX, sizeof(REBREQ));
