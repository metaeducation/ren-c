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

#include "sys-core.h"


// Size of crash buffers
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
// the PANIC native, and since it can be hijacked that offers hookability for
// "recoverable" forms of PANIC.
//
// coverity[+kill]
//
ATTRIBUTE_NO_RETURN void Panic_Core(
    const void *p,  // REBSER*, REBVAL*, or UTF-8 char*
    REBTCK tick,
    const char *file, // UTF8
    int line
){
    GC_Disabled = true;  // crashing is a legitimate reason to disable the GC

  #if DEBUG_FANCY_PANIC
    printf("C Source File %s, Line %d, Pointer %p\n", file, line, p);
    printf("At evaluator tick: %lu\n", cast(unsigned long, tick));

    fflush(stdout);  // release builds don't use <stdio.h>, but debug ones do
    fflush(stderr);  // ...so be helpful and flush any lingering debug output
  #else
    UNUSED(tick);
    UNUSED(file);
    UNUSED(line);
  #endif

    // Delivering a panic should not rely on printf()/etc. in release build.

    char buf[PANIC_BUF_SIZE + 1];
    buf[0] = '\0';

  #if !defined(NDEBUG) && 0
    //
    // These are currently disabled, because they generate too much junk.
    // Address Sanitizer gives a reasonable idea of the stack.
    //
    Dump_Info();
    Dump_Stack(FS_TOP, 0);
  #endif

  #if !defined(NDEBUG) && defined(HAVE_EXECINFO_AVAILABLE)
    void *backtrace_buf[1024];
    int n_backtrace = backtrace(  // GNU extension (but valgrind is better)
        backtrace_buf,
        sizeof(backtrace_buf) / sizeof(backtrace_buf[0])
    );
    fputs("Backtrace:\n", stderr);
    backtrace_symbols_fd(backtrace_buf, n_backtrace, STDERR_FILENO);
    fflush(stdout);
  #endif

    strncat(buf, Str_Panic_Directions, PANIC_BUF_SIZE - 0);

    strncat(buf, "\n", PANIC_BUF_SIZE - strsize(buf));

    if (not p) {
        strncat(
            buf,
            "Panic was passed C nullptr",
            PANIC_BUF_SIZE - strsize(buf)
        );
    }
    else switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8: // string might be empty...handle specially?
        strncat(
            buf,
            cast(const char*, p),
            PANIC_BUF_SIZE - strsize(buf)
        );
        break;

      case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p)); // don't mutate
      #if DEBUG_FANCY_PANIC
        #if 0
            //
            // It can sometimes be useful to probe here if the series is
            // valid, but if it's not valid then that could result in a
            // recursive call to panic and a stack overflow.
            //
            PROBE(s);
        #endif

        if (IS_VARLIST(s)) {
            printf("Series VARLIST detected.\n");
            REBCTX *context = cast(REBCTX*, s);  // CTX() does too much checking!
            if (KIND3Q_BYTE_UNCHECKED(CTX_ARCHETYPE(context)) == REB_ERROR) {
                printf("...and that VARLIST is of an ERROR!...");
                PROBE(context);
            }
        }
        Panic_Series_Debug(cast(REBSER*, s));
      #else
        UNUSED(s);
        strncat(buf, "valid series", PANIC_BUF_SIZE - strsize(buf));
      #endif
        break; }

      case DETECTED_AS_FREED_SERIES:
      #if DEBUG_FANCY_PANIC
        Panic_Series_Debug(m_cast(REBSER*, cast(const REBSER*, p)));
      #else
        strncat(buf, "freed series", PANIC_BUF_SIZE - strsize(buf));
      #endif
        break;

      case DETECTED_AS_CELL:
      case DETECTED_AS_END: {
        const REBVAL *v = cast(const REBVAL*, p);
      #if DEBUG_FANCY_PANIC
        if (KIND3Q_BYTE_UNCHECKED(v) == REB_ERROR) {
            printf("...panicking on an ERROR! value...");
            PROBE(v);
        }
        Panic_Value_Debug(v);
      #else
        UNUSED(v);
        strncat(buf, "value", PANIC_BUF_SIZE - strsize(buf));
      #endif
        break; }

      case DETECTED_AS_FREED_CELL:
      #if DEBUG_FANCY_PANIC
        Panic_Value_Debug(cast(const RELVAL*, p));
      #else
        strncat(buf, "freed cell", PANIC_BUF_SIZE - strsize(buf));
      #endif
        break;
    }

  #if DEBUG_FANCY_PANIC
    printf("%s\n", Str_Panic_Title);
    printf("%s\n", buf);
    fflush(stdout);
  #endif

  #if !defined(NDEBUG)
    //
    // Note: Emscripten actually gives a more informative stack trace in
    // its debug build through plain exit().  It has DEBUG_FANCY_PANIC but
    // also defines NDEBUG to turn off asserts.
    //
    debug_break();  // try to hook up to a C debugger - see %debug_break.h
  #endif

    exit (255);  // shell convention treats 255 as "exit code out of range"
}


//
//  panic: native [
//
//  "Terminate abnormally with a message, optionally diagnosing a value cell"
//
//      return: []  ; !!! notation for divergent functions?
//      reason [<opt> <meta> any-value!]
//          "Cause of the panic"
//      /value "Interpret reason as a value cell to debug dump, vs. a message"
//  ]
//
REBNATIVE(panic)
//
// Note: The ^reason parameter is meta so that `panic ~bad-word~` won't
// cause a parameter type check error, but actually runs this panic() code.
// Since it allows bad-word!, we treat it as a message if /VALUE is not used.
{
    INCLUDE_PARAMS_OF_PANIC;

    REBVAL *v = Meta_Unquotify(ARG(reason));  // remove quote level from @reason

    // Use frame tick (if available) instead of TG_Tick, so tick count dumped
    // is the exact moment before the PANIC ACTION! was invoked.
    //
  #if DEBUG_COUNT_TICKS
    REBTCK tick = frame_->tick;
  #else
    REBTCK tick = 0;
  #endif

    // panic() on the string value itself will report information about the
    // string cell...but panic() on UTF-8 character data assumes you mean to
    // report the contained message.  PANIC/VALUE for the latter intent.

    const void *p;

    if (REF(value)) {  // interpret reason as value to diagnose
        p = v;
    }
    else {  // interpret reason as a message
        if (IS_TEXT(v)) {
            p = VAL_UTF8_AT(v);
        }
        else if (IS_ERROR(v)) {
            p = VAL_CONTEXT(v);
        }
        else if (IS_BAD_WORD(v)) {
            p = STR_UTF8(VAL_BAD_WORD_LABEL(v));
        }
        else {
            assert(!"Called PANIC without /VALUE on non-TEXT!, non-ERROR!");
            p = v;
        }
    }

    Panic_Core(p, tick, FRM_FILE_UTF8(frame_), FRM_LINE(frame_));
}


//
//  fail: native [
//
//  {Early-boot version of FAIL (overridden by more complex usermode version)}
//
//      return: []  ; !!! Notation for divergent function?
//      'blame [<skip> meta-word! meta-path!]
//       reason [<end> <opt> any-value!]  ; permissive to avoid callsite error
//       /where [frame! any-word!]
//   ]
//
REBNATIVE(fail)
{
    INCLUDE_PARAMS_OF_FAIL;

  #if defined(NDEBUG)
    UNUSED(ARG(blame));
    UNUSED(ARG(where));
  #else
    printf("!!! Early-Boot FAIL, called fail: native [], not fail: func []\n");
    PROBE(ARG(reason));
    PROBE(ARG(blame));
    PROBE(ARG(where));

    rebElide(Lib(WRITE_STDOUT), Lib(DELIMIT), Lib(SPACE), ARG(reason));
  #endif

    panic (ARG(reason));
}
