//
//  File: %d-crash.c
//  Summary: "low level crash output"
//  Section: debug
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

#include "sys-core.h"


// Size of crash buffers
#define PANIC_TITLE_BUF_SIZE 80
#define PANIC_BUF_SIZE 512


//
//  Panic_Core: C
//
// See comments on `panic (...)` macro, which calls this routine.
//
ATTRIBUTE_NO_RETURN void Panic_Core(
    const void *p, // REBSER* (array, context, etc), REBVAL*, or UTF-8 char*
    const char *file,
    int line
) {
    if (p == NULL)
        p = "panic (...) was passed NULL"; // avoid later NULL tests

    // We are crashing, so a legitimate time to be disabling the garbage
    // collector.  (It won't be turned back on.)
    //
    GC_Disabled = TRUE;

#if defined(NDEBUG)
    UNUSED(file);
    UNUSED(line);
#else
    //
    // First thing's first in the debug build, make sure the file and the
    // line are printed out.
    //
    printf("C Source File %s, Line %d\n", file, line);

    // Generally Rebol does not #include <stdio.h>, but the debug build does.
    // It's often used for debug spew--as opposed to Debug_Fmt()--when there
    // is a danger of causing recursive errors if the problem is being caused
    // by I/O in the first place.  So flush anything lingering in the
    // standard output or error buffers
    //
    fflush(stdout);
    fflush(stderr);
#endif

    // Because the release build of Rebol does not link to printf or its
    // support functions, the crash buf is assembled into a buffer for
    // raw output through the host.
    //
    char title[PANIC_TITLE_BUF_SIZE + 1]; // account for null terminator
    char buf[PANIC_BUF_SIZE + 1]; // "

    title[0] = '\0';
    buf[0] = '\0';

#if !defined(NDEBUG)
    if (Reb_Opts && Reb_Opts->crash_dump) {
        Dump_Info();
        Dump_Stack(NULL, 0);
    }
#endif

    strncat(title, "PANIC()", PANIC_TITLE_BUF_SIZE - 0);

    strncat(buf, Str_Panic_Directions, PANIC_BUF_SIZE - 0);

    strncat(buf, "\n", PANIC_BUF_SIZE - strlen(buf));

    switch (Detect_Rebol_Pointer(p)) {
    case DETECTED_AS_UTF8: // string might be empty...handle specially?
        strncat(
            buf,
            cast(const char*, p),
            PANIC_BUF_SIZE - strlen(buf)
        );
        break;

    case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p)); // don't mutate
    #if !defined(NDEBUG)
        #if 0
            //
            // It can sometimes be useful to probe here if the series is
            // valid, but if it's not valid then that could result in a
            // recursive call to panic and a stack overflow.
            //
            PROBE(s);
        #endif

        if (GET_SER_FLAG(s, ARRAY_FLAG_VARLIST)) {
            printf("Series VARLIST detected.\n");
            REBCTX *context = CTX(s);
            if (CTX_TYPE(context) == REB_ERROR) {
                printf("...and that VARLIST is of an ERROR!...");
                PROBE(context);
            }
        }
        Panic_Series_Debug(cast(REBSER*, s));
    #else
        UNUSED(s);
        strncat(buf, "valid series", PANIC_BUF_SIZE - strlen(buf));
    #endif
        break; }

    case DETECTED_AS_FREED_SERIES:
    #if !defined(NDEBUG)
        Panic_Series_Debug(m_cast(REBSER*, cast(const REBSER*, p)));
    #endif
        strncat(buf, "freed series", PANIC_BUF_SIZE - strlen(buf));
        break;

    case DETECTED_AS_VALUE:
    case DETECTED_AS_END:
    #if !defined(NDEBUG)
        Panic_Value_Debug(cast(const REBVAL*, p));
    #else
        strncat(buf, "value", PANIC_BUF_SIZE - strlen(buf));
    #endif
        break;

    case DETECTED_AS_TRASH_CELL:
    #if !defined(NDEBUG)
        Panic_Value_Debug(cast(const RELVAL*, p));
    #endif
        strncat(buf, "trash cell", PANIC_BUF_SIZE - strlen(buf));
        break;
    }

#if !defined(NDEBUG)
    //
    // In a debug build, we'd like to try and cause a break so as not to lose
    // the state of the panic, which would happen if we called out to the
    // host kit's exit routine...
    //
    printf("%s\n", Str_Panic_Title);
    printf("%s\n", buf);
    fflush(stdout);
    debug_break(); // see %debug_break.h
#endif

    OS_CRASH(cb_cast(Str_Panic_Title), cb_cast(buf));

    DEAD_END;
}
