//
//  file: %d-dump.c
//  summary: "various debug output functions"
//  section: debug
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// Most of these low-level debug routines were leftovers from R3-Alpha, which
// had no RUNTIME_CHECKS build (and was perhaps frequently debugged without an
// IDE debugger).  After the open source release, Ren-C's reliance is on a
// more heavily checked build...so these routines were not used.
//
// They're being brought up to date to be included in the checked build only
// version of crash().  That should keep them in working shape.
//
// Note: These routines use `printf()`, that's only linked #if RUNTIME_CHECKS.
// Higher-level Rebol formatting should ultimately be using BLOCK! dialects,
// as opposed to strings with %s and %d.  Bear in mind the "z" modifier in
// printf is unavailable in C89, so if something might be 32-bit or 64-bit
// depending, it must be cast to unsigned long:
//
// http://stackoverflow.com/q/2125845
//

#include "sys-core.h"

#if DEBUG_FANCY_CRASH  // !!! separate switch, DEBUG_HAS_DUMP?

#ifdef _MSC_VER
#define snprintf _snprintf
#endif


//
//  Dump_Flex: C
//
void Dump_Flex(Flex* f, const char *memo)
{
    printf("Dump_Flex(%s) @ %p\n", memo, cast(void*, f));
    fflush(stdout);

    if (not f)
        return;

    printf(" wide: %d\n", cast(int, Flex_Wide(f)));
    if (Get_Stub_Flag(f, DYNAMIC)) {
        printf(" size: %ld\n", cast(long, Flex_Total(f)));
        printf(" bias: %d\n", cast(int, Flex_Bias(f)));
    }
    else
        printf(" size: 0\n");
    printf(" used: %d\n", cast(int, Flex_Used(f)));
    printf(" rest: %d\n", cast(int, Flex_Rest(f)));

    // flags includes len if non-dynamic
    printf(" flags: %lx\n", cast(unsigned long, f->header.bits));

    // info includes width
    printf(" info: %lx\n", cast(unsigned long, FLEX_INFO(f)));

    fflush(stdout);
}


//
//  Dump_Info: C
//
void Dump_Info(void)
{
    printf("^/--REBOL Kernel Dump--\n");

    printf("Evaluator:\n");
    printf("    Cycles:  %ld\n", cast(long, g_ts.total_eval_cycles));
    printf("    Counter: %d\n", cast(int, g_ts.eval_countdown));
    printf("    Dose:    %d\n", cast(int, g_ts.eval_dose));
    printf("    Signals: %lx\n", cast(unsigned long, g_ts.signal_flags));
    printf("    Sigmask: %lx\n", cast(unsigned long, g_ts.signal_mask));
    printf("    TOP_INDEX: %ld\n", cast(long, TOP_INDEX));

    printf("Memory/GC:\n");

    printf("    Ballast: %d\n", cast(int, g_gc.depletion));
    printf("    Disable: %s\n", g_gc.disabled ? "yes" : "no");
    printf("    Guarded: %d\n", cast(int, Flex_Used(g_gc.guarded)));
    fflush(stdout);
}


//
//  Dump_Stack: C
//
// Simple debug routine to list the function names on the stack and what the
// current feed value is.
//
void Dump_Stack(Level* L)
{
    if (L == nullptr)
        L = TOP_LEVEL;

    if (L == BOTTOM_LEVEL) {
        printf("<BOTTOM_LEVEL>\n");
        fflush(stdout);
        return;
    }

    const char *label;
    if (not Is_Action_Level(L))
        label = "<eval>";
    else
        label = Level_Label_Or_Anonymous_UTF8(L);

    printf("LABEL: %s @ FILE: %s @ LINE: %" PRIuPTR "\n",  // uintptr_t format
        label,
        File_UTF8_Of_Level(L),
        maybe Line_Number_Of_Level(L)
    );

    Dump_Stack(L->prior);
}



#endif // DUMP is picked up by scan regardless of #ifdef, must be defined


//
//  dump: native [
//
//  "Temporary debug dump"
//
//      return: []
//      @value [word!]
//  ]
//
DECLARE_NATIVE(DUMP)
{
    INCLUDE_PARAMS_OF_DUMP;

  #if RUNTIME_CHECKS
    Element* v = Element_ARG(VALUE);

    PROBE(v);

    if (Is_Word(v)) {
        printf("=> ");

        Value* spare = Get_Word(
            SPARE, v, SPECIFIED
        ) except (Error* e) {
            printf("!!! ERROR FETCHING WORD FOR DUMP !!!");
            PROBE(e);
            return TRIPWIRE;
        }

        PROBE(spare);
    }

    return TRIPWIRE;
  #else
    UNUSED(ARG(VALUE));
    panic (Error_Checked_Build_Only_Raw());
  #endif
}
