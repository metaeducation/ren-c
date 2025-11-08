//
//  file: %d-backtrace.c
//  summary: "Alternative C Stack Backtrace Implementation"
//  section: debug
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2025 Ren-C Open Source Contributors
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
// A. This file stands alone because it cannot include %sys-core.h, due to
//    *many* naming conflicts that arise with Windows's <dbghelp.h>... it is
//    not enough to simply `#define REBOL_LEVEL_SHORTHAND_MACROS` to 0.
//
// B. The backtrace() in <execinfo.h> on certain Unixlike platforms is not
//    very good.  Not only can it not show line numbers, it only shows function
//    names if you compile with `-rdynamic` and do NOT compile with
//    `-fvisibility=hidden`...
//
//    ...*HOWEVER* - if you do not use `-fvisibility=hidden` you are at risk
//    of encountering symbol conflicts with any shared libraries that you load
//    into the same process.  This is known as an "ODR violation", and it can
//    lead to bizarre behavior:
//
//      https://en.wikipedia.org/wiki/One_Definition_Rule
//
//    It's basically impossible to guarantee that you won't have symbol
//    conflicts that are incidental to `extern` variables you use to share
//    between your own .obj files... that another shared library might use the
//    same name.  All modern systems depend on hidden visibility in order to
//    work with dynamic libraries safely.
//
//    So if at all possible, it's better to rely on Address Sanitizer to
//    provide any backtrace that you are interested in.  Unfortunately, this
//    generally means crashing the program.  In any case, beware of what
//    happens if you disable `-fvisibility=hidden`, and only do that in
//    controlled test cases!
//

#include "reb-config.h"  // can't include %sys-core.h (see [A])

#if DEBUG_FANCY_CRASH
  #if HAVE_EXECINFO_H_AVAILABLE
    #include <stdlib.h>  // free()

    #include <execinfo.h>
    #include <unistd.h>  // STDERR_FILENO
  #endif

  #if defined(_MSC_VER)  // MSVC compiler on windows, honors this #pragma
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib, "dbghelp.lib")
  #endif

    #include "assert.h"
    #include "needful/needful.h"
    #include "c-extras.h"

    #include <stdio.h>

    #define Printf_Stderr(...) do { \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr); /* stderr not necessarily unbuffered in all cases */ \
    } while (0)
#endif


//
//  Print_C_Stack_Trace_If_Available: C
//
// See remarks at top of file about the sketchy nature of this feature.
//
void Print_C_Stack_Trace_If_Available(void)
{
#if (! DEBUG_FANCY_CRASH)
    return;  // nothing to do (and not necessarily any <stdio.h> available))
#else
    Printf_Stderr("\n=== BEGIN Print_C_Stack_Trace_If_Available() ===\n\n");

  #if defined(_MSC_VER)  // included dbghelp.lib via #pragma above

    void* stack[64];
    HANDLE process = GetCurrentProcess();

    SymInitialize(process, nullptr, TRUE);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    WORD frames = CaptureStackBackTrace(0, 64, stack, nullptr);
    Printf_Stderr("(%d frames):\n", frames);

    SYMBOL_INFO *symbol = cast(SYMBOL_INFO*, calloc(
        sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1
    ));
    if (not symbol)
        Printf_Stderr("  <could not allocate SYMBOL_INFO>\n");
    else {
        symbol->MaxNameLen = 255;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

        for (WORD i = 0; i < frames; i++) {
            DWORD64 addr = i_cast(DWORD64, stack[i]);

            if (SymFromAddr(process, addr, 0, symbol)) {
                IMAGEHLP_LINE64 line;
                line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                DWORD displacement;

                if (SymGetLineFromAddr64(process, addr, &displacement, &line)) {
                    Printf_Stderr(
                        "  %2d: %s() at %s:%lu [0x%llx]\n",
                        i,
                        symbol->Name,
                        line.FileName,
                        line.LineNumber,
                        cast(unsigned long long, symbol->Address)
                    );
                }
                else {
                    Printf_Stderr(
                        "  %2d: %s() [0x%llx]\n",
                        i,
                        symbol->Name,
                        cast(unsigned long long, symbol->Address)
                    );
                }
            }
            else
                Printf_Stderr("  %2d: <unknown> [0x%p]\n", i, stack[i]);
        }
        free(symbol);
    }
    SymCleanup(process);

  #elif HAVE_EXECINFO_H_AVAILABLE  // POSIX (Linux/macOS): Use backtrace

    void *backtrace_buf[128];
    int n_backtrace = backtrace(
        backtrace_buf,
        sizeof(backtrace_buf) / sizeof(backtrace_buf[0])
    );

    Printf_Stderr("Crappy execinfo.h backtrace (%d frames):\n\n", n_backtrace);

    char **symbols = backtrace_symbols(backtrace_buf, n_backtrace);
    if (symbols) {  // if `-fvisibility=hidden` used, this won't work, see [B]
        for (int i = 0; i < n_backtrace; i++) {
            Printf_Stderr("  %2d: %s\n", i, symbols[i]);
        }
        free(symbols);
    }
    else {
        // Fallback: just dump addresses if symbol resolution fails
        backtrace_symbols_fd(backtrace_buf, n_backtrace, STDERR_FILENO);
    }

    Printf_Stderr("\nNo line #s.  And no symbols if built w/o `-rdynamic`\n");
    Printf_Stderr("...nor if built with `-fvisibility=hidden`, BUT BEWARE!\n");
    Printf_Stderr("Hidden is important, see d-backtrace.c about ODR bugs.\n");
    Printf_Stderr("Prefer using Address Sanitizer vs. un-hiding symbols.\n");

  #else

    Printf_Stderr("(Stack trace not available on this platform)\n");

  #endif

    Printf_Stderr("\n=== END Print_C_Stack_Trace_If_Available() ===\n\n");

#endif  // DEBUG_FANCY_CRASH
}
