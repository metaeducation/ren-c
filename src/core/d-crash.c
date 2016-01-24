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
#define PANIC_TITLE_BUF_SIZE 80
#define PANIC_BUF_SIZE 512


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
ATTRIBUTE_NO_RETURN void Panic_Core(
    REBCNT id,
    REBCTX *opt_error,
    va_list *varargs_ptr
) {
    char title[PANIC_TITLE_BUF_SIZE + 1]; // account for null terminator
    char message[PANIC_BUF_SIZE + 1]; // "

    title[0] = '\0';
    message[0] = '\0';

    if (opt_error) {
        ASSERT_CONTEXT(opt_error);
        assert(CTX_TYPE(opt_error) == REB_ERROR);
        assert(id == 0);
        id = ERR_NUM(opt_error);
    }

    // We are crashing, so a legitimate time to be disabling the garbage
    // collector.  (It won't be turned back on.)
    //
    GC_Disabled++;

#if !defined(NDEBUG)
    if (Reb_Opts && Reb_Opts->crash_dump) {
        Dump_Info();
        Dump_Stack(0, 0);
    }
#endif

    strncat(title, "PANIC #", PANIC_TITLE_BUF_SIZE - 0);
    Form_Int(b_cast(title + strlen(title)), id); // !!! no bounding...

    strncat(message, Str_Panic_Directions, PANIC_BUF_SIZE - 0);

#if !defined(NDEBUG)
    // In debug builds, we may have the file and line number to report if
    // the call to Panic_Core originated from the `panic` macro.  But we
    // will not if the panic is being called from a Make_Error call that
    // is earlier than errors can be made...

    if (TG_Erroring_C_File) {
        strncat(message, "C Source File ", PANIC_BUF_SIZE - strlen(message));
        strncat(message, TG_Erroring_C_File, PANIC_BUF_SIZE - strlen(message));
        strncat(message, ", Line ", PANIC_BUF_SIZE - strlen(message));
        Form_Int(b_cast(message + strlen(message)), TG_Erroring_C_Line); // !
        strncat(message, "\n", PANIC_BUF_SIZE - strlen(message));
    }
#endif

    if (PG_Boot_Phase < BOOT_LOADED) {
        strncat(message, title, PANIC_BUF_SIZE - strlen(message));
        strncat(
            message,
            "\n** Boot Error: (string table not decompressed yet)",
            PANIC_BUF_SIZE - strlen(message)
        );
    }
    else if (PG_Boot_Phase < BOOT_ERRORS && id < RE_INTERNAL_MAX) {
        //
        // We are panic'ing on one of the errors that can occur during
        // boot (e.g. before Make_Error() be assured to run).  So we use
        // the C string constant that was formed by %make-boot.r and
        // compressed in the boot block.
        //
        const char *format =
            cs_cast(BOOT_STR(RS_ERROR, id - RE_INTERNAL_FIRST));

        // !!! These strings currently do not heed arguments, so if they
        // use a format specifier it will be ignored.  Technically it
        // *may* be possible at some levels of boot to use the args.  If it
        // becomes a priority then find a way to safely report them
        // (perhaps a subset like integer!, otherwise just print type #?)
        //
        assert(varargs_ptr && !opt_error);

        strncat(
            message, "\n** Boot Error: ", PANIC_BUF_SIZE - strlen(message)
        );
        strncat(message, format, PANIC_BUF_SIZE - strlen(message));
    }
    else if (PG_Boot_Phase < BOOT_ERRORS && id >= RE_INTERNAL_MAX) {
        strncat(message, title, PANIC_BUF_SIZE - strlen(message));
        strncat(
            message,
            "\n** Boot Error: (error object table not initialized yet)",
            PANIC_BUF_SIZE - strlen(message)
        );
    }
    else {
        // The system should be theoretically able to make and mold errors.
        //
        // !!! If you're trying to panic *during* error molding this
        // is obviously not going to not work.  All errors pertaining to
        // molding errors should audited to be in the Boot: category.
        //
        // !!! As a bigger question, whether a `panic` means "stop the
        // system right now for fear of data corruption` or "stop running"
        // would guide whether this should effectively "blue-screen" or
        // continue trying to use series and other mechanisms in the
        // report.  The stronger meaning of panic would indicate that this
        // *not* try to decode series or values in the report, for fear
        // of tripping over a cascading bug that might do bad things.
        // In which case, this branch would be removed.

        REB_MOLD mo;
        REBSER *bytes;

        REBVAL error;
        VAL_INIT_WRITABLE_DEBUG(&error);

        CLEARS(&mo);
        SET_FLAG(mo.opts, MOPT_LIMIT);
        mo.limit = PANIC_BUF_SIZE - strlen(message); // codepoints, not bytes

        Push_Mold(&mo);

        if (opt_error) {
            assert(!varargs_ptr);
            Val_Init_Error(&error, opt_error);
        }
        else {
            // We aren't explicitly passed a Rebol ERROR! object, but we
            // consider it "safe" to make one since we're past BOOT_ERRORS

            Val_Init_Error(&error, Make_Error_Core(id, FALSE, varargs_ptr));
        }

        Mold_Value(&mo, &error, FALSE);
        bytes = Pop_Molded_UTF8(&mo);

        strncat(
            message, s_cast(BIN_HEAD(bytes)), PANIC_BUF_SIZE - strlen(message)
        );

        Free_Series(bytes);
    }

    OS_CRASH(cb_cast(Str_Panic_Title), cb_cast(message));

    // Note that since we crash, we never return so that the caller can run
    // a va_end on the passed-in args.  This is illegal in the general case:
    //
    //    http://stackoverflow.com/a/587139/211160

    DEAD_END;
}
