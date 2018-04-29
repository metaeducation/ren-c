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

#ifdef HAVE_EXECINFO_AVAILABLE
    #include <execinfo.h>
    #include <unistd.h>  // STDERR_FILENO
#endif


//
//  Panic_Core: C
//
// Abnormal termination of Rebol.  The debug build is designed to present
// as much diagnostic information as it can on the passed-in pointer, which
// includes where a REBSER* was allocated or freed.  Or if a REBVAL* is
// passed in it tries to say what tick it was initialized on and what series
// it lives in.  If the pointer is a simple UTF-8 string pointer, then that
// is delivered as a message.
//
// This can be triggered via the macros panic() and panic_at(), which are
// unsalvageable situations in the core code.  It can also be triggered by
// the PANIC and PANIC-VALUE natives, which in turn can be triggered by the
// rebPanic() API.  (Since PANIC and PANIC-VALUE may be hijacked, this offers
// hookability for "recoverable" forms of PANIC.)
//
// coverity[+kill]
//
ATTRIBUTE_NO_RETURN void Panic_Core(
    const void *p, // REBSER* (array, context, etc), REBVAL*, or UTF-8 char*
    REBTCK tick,
    const char *file, // UTF8
    int line
){
    if (p == NULL)
        p = "panic (...) was passed NULL"; // avoid later NULL tests

    // We are crashing, so a legitimate time to be disabling the garbage
    // collector.  (It won't be turned back on.)
    //
    GC_Disabled = TRUE;

#if defined(NDEBUG)
    UNUSED(tick);
    UNUSED(file);
    UNUSED(line);
#else
    //
    // First thing's first in the debug build, make sure the file and the
    // line are printed out, as well as the current evaluator tick.
    //
    printf("C Source File %s, Line %d, Pointer %p\n", file, line, p);
    printf("At evaluator tick: %lu\n", cast(unsigned long, tick));

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

#if !defined(NDEBUG) && 0
    //
    // These are currently disabled, because they generate too much junk.
    // Address Sanitizer gives a reasonable idea of the stack.
    //
    Dump_Info();
    Dump_Stack(NULL, 0);
#endif

#if !defined(NDEBUG) && defined(HAVE_EXECINFO_AVAILABLE)
    //
    // Backtrace is a GNU extension.  There should be a way to turn this on
    // or off, as it will be redundant with a valgrind or address sanitizer
    // trace (and contain less information).
    //
    void *backtrace_buf[1024];
    int n_backtrace = backtrace(
        backtrace_buf,
        sizeof(backtrace_buf) / sizeof(backtrace_buf[0])
    );
    fputs("Backtrace:\n", stderr);
    backtrace_symbols_fd(backtrace_buf, n_backtrace, STDERR_FILENO);
    fflush(stdout);
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
    #if defined(NDEBUG)
        strncat(buf, "freed series", PANIC_BUF_SIZE - strlen(buf));
    #else
        Panic_Series_Debug(m_cast(REBSER*, cast(const REBSER*, p)));
    #endif
        break;

    case DETECTED_AS_VALUE:
    case DETECTED_AS_END: {
        const REBVAL *v = cast(const REBVAL*, p);
    #if defined(NDEBUG)
        UNUSED(v);
        strncat(buf, "value", PANIC_BUF_SIZE - strlen(buf));
    #else
        if (NOT_END(v) and IS_ERROR(v)) {
            printf("...panicking on an ERROR! value...");
            PROBE(v);
        }
        Panic_Value_Debug(v);
    #endif
        break; }

    case DETECTED_AS_TRASH_CELL:
    #if defined(NDEBUG)
        strncat(buf, "trash cell", PANIC_BUF_SIZE - strlen(buf));
    #else
        Panic_Value_Debug(cast(const RELVAL*, p));
    #endif
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

    // 255 is standardized as "exit code out of range", but it seems like the
    // best choice for an anomalous exit.
    //
    exit (255);
}


//
//  panic: native [
//
//  "Cause abnormal termination of Rebol (dumps debug info in debug builds)"
//
//      reason [string! error!]
//          "Message to report (evaluation not counted in ticks)"
//  ]
//
REBNATIVE(panic)
{
    INCLUDE_PARAMS_OF_PANIC;

    REBVAL *v = ARG(reason);
    void *p;

    // panic() on the string value itself would report information about the
    // string cell...but panic() on UTF-8 character data assumes you mean to
    // report the contained message.  PANIC-VALUE for the latter intent.
    //
    if (IS_STRING(v)) {
        REBSIZ offset;
        REBSIZ size;
        REBSER *temp = Temp_UTF8_At_Managed(&offset, &size, v, VAL_LEN_AT(v));

        p = BIN_AT(temp, offset); // UTF-8 data
    }
    else {
        assert(IS_ERROR(v));
        p = VAL_CONTEXT(v);
    }

    // Note that by using the frame's tick instead of TG_Tick, we don't count
    // the evaluation of the value argument.  Hence the tick count shown in
    // the dump would be the one that would queue up right to the exact moment
    // *before* the PANIC ACTION! was invoked.
    //
  #ifdef DEBUG_COUNT_TICKS
    Panic_Core(p, frame_->tick, FRM_FILE_UTF8(frame_), FRM_LINE(frame_));
  #else
    const REBTCK tick = 0;
    Panic_Core(p, tick, FRM_FILE_UTF8(frame_), FRM_LINE(frame_));
  #endif
}


//
//  panic-value: native [
//
//  "Cause abnormal termination of Rebol, with diagnostics on a value cell"
//
//      value [any-value!]
//          "Suspicious value to panic on (debug build shows diagnostics)"
//  ]
//
REBNATIVE(panic_value)
{
    INCLUDE_PARAMS_OF_PANIC_VALUE;

  #ifdef DEBUG_TRACK_TICKS
    //
    // Use frame tick (if available) instead of TG_Tick, so tick count dumped
    // is the exact moment before the PANIC-VALUE ACTION! was invoked.
    //
    Panic_Core(
        ARG(value), frame_->tick, FRM_FILE_UTF8(frame_), FRM_LINE(frame_)
    );
  #else
    const REBTCK tick = 0;
    Panic_Core(ARG(value), tick, FRM_FILE_UTF8(frame_), FRM_LINE(frame_));
  #endif
}
