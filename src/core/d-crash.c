//
//  file: %d-crash.c
//  summary: "low level crash output"
//  section: debug
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
#define CRASH_TITLE_BUF_SIZE 80
#define CRASH_MSG_BUF_SIZE 512

#ifdef HAVE_EXECINFO_AVAILABLE
    #include <execinfo.h>
    #include <unistd.h>  // STDERR_FILENO
#endif


//
//  Crash_Core: C
//
// Abnormal termination of Rebol.  The debug build is designed to present
// as much diagnostic information as it can on the passed-in pointer, which
// includes where a Flex* was allocated or freed.  Or if a Value* is
// passed in it tries to say what tick it was initialized on and what series
// it lives in.  If the pointer is a simple UTF-8 string pointer, then that
// is delivered as a message.
//
// This can be triggered via the macros crash() and crash_at(), which are
// unsalvageable situations in the core code.  It can also be triggered by
// the CRASH native.
//
// coverity[+kill]
//
ATTRIBUTE_NO_RETURN void Crash_Core(
    const void *p, // Flex* (array, context, etc), Value*, or UTF-8 char*
    Tick tick,
    const char *file, // UTF8
    int line
){
    // We are crashing, so a legitimate time to be disabling the garbage
    // collector.  (It won't be turned back on.)
    //
    GC_Disabled = true;

  #if NO_RUNTIME_CHECKS
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
    char title[CRASH_TITLE_BUF_SIZE + 1]; // account for null terminator
    char buf[CRASH_MSG_BUF_SIZE + 1]; // "

    title[0] = '\0';
    buf[0] = '\0';

  #if RUNTIME_CHECKS && 0
    //
    // These are currently disabled, because they generate too much junk.
    // Address Sanitizer gives a reasonable idea of the stack.
    //
    Dump_Info();
    Dump_Stack(TOP_LEVEL, 0);
  #endif

  #if RUNTIME_CHECKS && defined(HAVE_EXECINFO_AVAILABLE)
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

    strncat(title, "CRASH()", CRASH_TITLE_BUF_SIZE - 0);

    strncat(buf, g_crash_directions, CRASH_MSG_BUF_SIZE - 0);

    strncat(buf, "\n", CRASH_MSG_BUF_SIZE - strlen(buf));

    if (not p) {

        strncat(
            buf,
            "Crash was passed C nullptr",
            CRASH_MSG_BUF_SIZE - strlen(buf)
        );

    } else switch (Detect_Rebol_Pointer(p)) {

    case DETECTED_AS_UTF8: // string might be empty...handle specially?
        strncat(
            buf,
            cast(const char*, p),
            CRASH_MSG_BUF_SIZE - strlen(buf)
        );
        break;

    case DETECTED_AS_STUB: {
        Flex* s = m_cast(Flex*, cast(const Flex*, p)); // don't mutate
      #if RUNTIME_CHECKS
        #if 0
            //
            // It can sometimes be useful to probe here if the series is
            // valid, but if it's not valid then that could result in a
            // recursive call to crash() and a stack overflow.
            //
            PROBE(s);
        #endif

        if (Get_Array_Flag(s, IS_VARLIST)) {
            printf("Series VARLIST detected.\n");
            VarList* context = CTX(s);
            if (CTX_TYPE(context) == TYPE_ERROR) {
                printf("...and that VARLIST is of an ERROR!...");
                PROBE(context);
            }
        }
        Crash_On_Flex_Debug(cast(Flex*, s));
      #else
        UNUSED(s);
        strncat(buf, "valid series", CRASH_MSG_BUF_SIZE - strlen(buf));
      #endif
        break; }

    case DETECTED_AS_FREE:
      #if NO_RUNTIME_CHECKS
        strncat(buf, "freed series", CRASH_MSG_BUF_SIZE - strlen(buf));
      #else
        Crash_On_Flex_Debug(m_cast(Flex*, cast(const Flex*, p)));
      #endif
        break;

    case DETECTED_AS_CELL:
    case DETECTED_AS_END: {
        const Value* v = cast(const Value*, p);
      #if NO_RUNTIME_CHECKS
        UNUSED(v);
        strncat(buf, "value", CRASH_MSG_BUF_SIZE - strlen(buf));
      #else
        if (NOT_END(v) and Is_Error(v)) {
            printf("...crashing on an ERROR! value...");
            PROBE(v);
        }
        Crash_On_Value_Debug(v);
      #endif
        break; }
    }

  #if RUNTIME_CHECKS
    //
    // In a debug build, we'd like to try and cause a break so as not to lose
    // the state of the crash(), which would happen if we called out to the
    // host kit's exit routine...
    //
    printf("%s\n", g_crash_title);
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
//  crash: native [
//
//  "Cause abnormal termination of Rebol (dumps debug info in debug builds)"
//
//      reason [<end> text! error!]
//          "Message to report (evaluation not counted in ticks)"
//  ]
//
DECLARE_NATIVE(CRASH)
{
    INCLUDE_PARAMS_OF_CRASH;

    Value* v = ARG(REASON);
    void *p;

    // crash() on the string value itself would report information about the
    // string cell...but crash() on UTF-8 character data assumes you mean to
    // report the contained message.
    //
    if (Is_Nulled(v)) {  // <end>
        p = nullptr;
    }
    else if (Is_Text(v)) {
        Size offset;
        Size size;
        Binary* temp = Temp_UTF8_At_Managed(
            &offset, &size, v, Cell_Series_Len_At(v)
        );

        p = Binary_At(temp, offset); // UTF-8 data
    }
    else {
        assert(Is_Error(v));
        p = Cell_Varlist(v);
    }

    // Note that by using the frame's tick instead of TG_Tick, we don't count
    // the evaluation of the value argument.  Hence the tick count shown in
    // the dump would be the one that would queue up right to the exact moment
    // *before* the CRASH ACTION! was invoked.

    Option(String*) file = File_Of_Level(level_);
    const char* file_utf8;
    if (file) {
        Binary* bin = Make_Utf8_From_String(unwrap file);  // leak ok, crash()
        file_utf8 = cast(const char*, Binary_Head(bin));
    }
    else
      file_utf8 = "(anonymous)";

  #if DEBUG_COUNT_TICKS
    Crash_Core(p, level_->tick, file_utf8, LVL_LINE(level_));
  #else
    const Tick tick = 0;
    Crash_Core(p, tick, file_utf8, LVL_LINE(level_));
  #endif
}
