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
**  Module:  d-crash.c
**  Summary: low level crash output
**  Section: debug
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

// Size of crash buffers
#define PANIC_TITLE_SIZE 80
#define PANIC_MESSAGE_SIZE 512


//
//  Panic_Core: C
// 
// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
// 
// Print a failure message and abort.  The code adapts to several
// different load stages of the system, and uses simpler ways to
// report the error when the boot has not progressed enough to
// use the more advanced modes.  This allows the same interface
// to be used for `panic Error_XXX(...)` and `fail (Error_XXX(...))`.
//
ATTRIBUTE_NO_RETURN void Panic_Core(REBCNT id, REBSER *maybe_frame, va_list *args)
{
    char title[PANIC_TITLE_SIZE];
    char message[PANIC_MESSAGE_SIZE];

    title[0] = '\0';
    message[0] = '\0';

    if (maybe_frame) {
        assert(id == 0);
        id = ERR_NUM(maybe_frame);
    }

    // We are crashing, so a legitimate time to be disabling the garbage
    // collector.  (It won't be turned back on.)
    GC_Disabled++;

    if (Reb_Opts && Reb_Opts->crash_dump) {
        Dump_Info();
        Dump_Stack(0, 0);
    }

    strncat(title, "PANIC #", PANIC_TITLE_SIZE - 1);
    Form_Int(b_cast(title + strlen(title)), id); // !!! no bounding...

    strncat(message, Str_Panic_Directions, PANIC_MESSAGE_SIZE - 1);

#if !defined(NDEBUG)
    // In debug builds, we may have the file and line number to report if
    // the call to Panic_Core originated from the `panic` macro.  But we
    // will not if the panic is being called from a Make_Error call that
    // is earlier than errors can be made...

    if (TG_Erroring_C_File) {
        Form_Args(
            b_cast(message + strlen(message)),
            PANIC_MESSAGE_SIZE - 1 - strlen(message),
            "C Source File %s, Line %d\n",
            TG_Erroring_C_File,
            TG_Erroring_C_Line,
            NULL
        );
    }
#endif

    if (PG_Boot_Phase < BOOT_LOADED) {
        strncat(message, title, PANIC_MESSAGE_SIZE - 1);
        strncat(
            message,
            "\n** Boot Error: (string table not decompressed yet)",
            PANIC_MESSAGE_SIZE - 1
        );
    }
    else if (PG_Boot_Phase < BOOT_ERRORS && id < RE_INTERNAL_MAX) {
        // We are panic'ing on one of the errors that can occur during
        // boot (e.g. before Make_Error() be assured to run).  So we use
        // the C string constant that was formed by %make-boot.r and
        // compressed in the boot block.
        //
        // Note: These strings currently do not allow arguments.

        const char *format =
            cs_cast(BOOT_STR(RS_ERROR, id - RE_INTERNAL_FIRST));
        assert(args && !maybe_frame);
        strncat(message, "\n** Boot Error: ", PANIC_MESSAGE_SIZE - 1);
        Form_Args_Core(
            b_cast(message + strlen(message)),
            PANIC_MESSAGE_SIZE - 1 - strlen(message),
            format,
            args
        );
    }
    else if (PG_Boot_Phase < BOOT_ERRORS && id >= RE_INTERNAL_MAX) {
        strncat(message, title, PANIC_MESSAGE_SIZE - 1);
        strncat(
            message,
            "\n** Boot Error: (error object table not initialized yet)",
            PANIC_MESSAGE_SIZE - 1
        );
    }
    else {
        // The system should be theoretically able to make and mold errors.
        //
        // !!! If you're trying to panic *during* error molding this
        // is obviously not going to not work.  All errors pertaining to
        // molding errors should audited to be in the Boot: category.

        REBVAL error;

        if (maybe_frame) {
            assert(!args);
            Val_Init_Error(&error, maybe_frame);
        }
        else {
            // We aren't explicitly passed a Rebol ERROR! object, but we
            // consider it "safe" to make one since we're past BOOT_ERRORS

            Val_Init_Error(&error, Make_Error_Core(id, args));
        }

        Form_Args(
            b_cast(message + strlen(message)),
            PANIC_MESSAGE_SIZE - 1 - strlen(message),
            "%v",
            &error,
            NULL
        );
    }

    OS_CRASH(cb_cast(Str_Panic_Title), cb_cast(message));

    // Note that since we crash, we never return so that the caller can run
    // a va_end on the passed-in args.  This is illegal in the general case:
    //
    //    http://stackoverflow.com/a/587139/211160

    DEAD_END;
}

