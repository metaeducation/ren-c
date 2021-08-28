//
//  File: %f-device.c
//  Summary: "Device Registration and Polling Dispatch"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// This file contains the minimum entry points by which an extension can
// register interest in being polled for activity.
//
// It is the last vestige of R3-Alpha's "device" codebase.  That system tried
// to abstract operating system services in a way that would be used by Rebol
// but spoke only in terms of pure C structs.  This was mostly make-work, as
// the layer only existed for the purpose of interfacing the OS with values
// coming from--or going to--Rebol.  The abstraction was inflexible: integer
// based table of routines, which took a single argument and made the
// applicable parameterization nearly impossible to see.
//
// So basically all the device layer was removed.  The services it tried to
// provide in terms of hooking the GC are now all done better via the API.
//
// Yet there is still a need for extensions to be able to inject some code
// into the event loop--whatever that event loop may be.  Ren-C is agnostic
// on that, as it wants to work in mediums like WebAssembly where the browser
// event loop is what should be used.  But at time of writing, it offers an
// extension with the historical EVENT! of Rebol.
//

#include <string.h>
#include <stdlib.h>

#include "sys-core.h"


//
//  OS_Poll_Devices: C
//
// Poll devices for activity.  Returns count of devices that changed status.
//
int OS_Poll_Devices(void)
{
    int num_changed = 0;

    REBDEV *dev = PG_Device_List;
    for (; dev != nullptr; dev = dev->next) {
        if (dev->poll())
            ++num_changed;
    }

    return num_changed;
}


//
//  OS_Register_Device: C
//
// This puts a device into the list the system walks to poll when a WAIT
// loop is running.
//
// !!! It might be more useful if REBDEV was an actual REBVAL, so it ould be
// put into objects.  But for now, it's just a tiny struct.
//
REBDEV *OS_Register_Device(const char* name, DEVICE_POLL_CFUNC *poll) {
    REBDEV *dev = TRY_ALLOC(REBDEV);
    dev->name = name;
    dev->poll = poll;

    dev->next = PG_Device_List;
    PG_Device_List = dev;

    return dev;
}


//
//  OS_Unregister_Device: C
//
// The extension calling Unregister_Device is responsible for cleaning up any
// pending requests for that device.
//
void OS_Unregister_Device(REBDEV *dev) {

    if (PG_Device_List == dev)
        PG_Device_List = dev->next;
    else {
        REBDEV *temp = PG_Device_List;
        while (temp->next != dev)
            temp = temp->next;
        temp->next = dev->next;
    }

    FREE(REBDEV, dev);
}
