//
//  File: %d-dump.c
//  Summary: "various debug output functions"
//  Section: debug
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// had no DEBUG build (and was perhaps frequently debugged without an IDE
// debugger).  After the open source release, Ren-C's reliance is on a
// more heavily checked debug build...so these routines were not used.
//
// They're being brought up to date to be included in the debug build only
// version of panic().  That should keep them in working shape.
//
// Note: These routines use `printf()`, which is only linked in DEBUG builds.
// Higher-level Rebol formatting should ultimately be using BLOCK! dialects,
// as opposed to strings with %s and %d.  Bear in mind the "z" modifier in
// printf is unavailable in C89, so if something might be 32-bit or 64-bit
// depending, it must be cast to unsigned long:
//
// http://stackoverflow.com/q/2125845
//

#include "sys-core.h"

#if DEBUG_FANCY_PANIC  // !!! separate switch, DEBUG_HAS_DUMP?

#ifdef _MSC_VER
#define snprintf _snprintf
#endif


//
//  Dump_Series: C
//
void Dump_Series(Series(*) s, const char *memo)
{
    printf("Dump_Series(%s) @ %p\n", memo, cast(void*, s));
    fflush(stdout);

    if (s == NULL)
        return;

    printf(" wide: %d\n", cast(int, Series_Wide(s)));
    if (Get_Series_Flag(s, DYNAMIC)) {
        printf(" size: %ld\n", cast(unsigned long, Series_Total(s)));
        printf(" bias: %d\n", cast(int, Series_Bias(s)));
    }
    else
        printf(" size: 0\n");
    printf(" used: %d\n", cast(int, Series_Used(s)));
    printf(" rest: %d\n", cast(int, Series_Rest(s)));

    // flags includes len if non-dynamic
    printf(" flags: %lx\n", cast(unsigned long, s->leader.bits));

    // info includes width
    printf(" info: %lx\n", cast(unsigned long, SERIES_INFO(s)));

    fflush(stdout);
}


//
//  Dump_Info: C
//
void Dump_Info(void)
{
    printf("^/--REBOL Kernel Dump--\n");

    printf("Evaluator:\n");
    printf("    Cycles:  %ld\n", cast(unsigned long, g_ts.total_eval_cycles));
    printf("    Counter: %d\n", cast(int, g_ts.eval_countdown));
    printf("    Dose:    %d\n", cast(int, g_ts.eval_dose));
    printf("    Signals: %lx\n", cast(unsigned long, g_ts.eval_signals));
    printf("    Sigmask: %lx\n", cast(unsigned long, g_ts.eval_sigmask));
    printf("    TOP_INDEX: %ld\n", cast(unsigned long, TOP_INDEX));

    printf("Memory/GC:\n");

    printf("    Ballast: %d\n", cast(int, g_gc.depletion));
    printf("    Disable: %s\n", g_gc.disabled ? "yes" : "no");
    printf("    Guarded Nodes: %d\n", cast(int, Series_Used(g_gc.guarded)));
    fflush(stdout);
}


//
//  Dump_Stack: C
//
// Simple debug routine to list the function names on the stack and what the
// current feed value is.
//
void Dump_Stack(Level(*) L)
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
    else if (not L->label)
        label = "<anonymous>";
    else
        label = String_UTF8(unwrap(L->label));

    printf("LABEL: %s @ FILE: %s @ LINE: %" PRIuPTR "\n",  // uintptr_t format
        label,
        File_UTF8_Of_Level(L),
        LineNumber_Of_Level(L)
    );

    Dump_Stack(L->prior);
}



#endif // DUMP is picked up by scan regardless of #ifdef, must be defined


//
//  dump: native [
//
//  "Temporary debug dump"
//
//      return: <void>
//      :value [word!]
//  ]
//
DECLARE_NATIVE(dump)
{
    INCLUDE_PARAMS_OF_DUMP;

#ifdef NDEBUG
    UNUSED(ARG(value));
    fail (Error_Debug_Only_Raw());
#else
    REBVAL *v = ARG(value);

    PROBE(v);
    printf("=> ");
    if (IS_WORD(v)) {
        const REBVAL* var = try_unwrap(Lookup_Word(v, SPECIFIED));
        if (not var) {
            PROBE("\\unbound\\");
        }
        else if (Is_Nulled(var)) {
            PROBE("\\null\\");
        }
        else
            PROBE(var);
    }

    return VOID;
#endif
}
