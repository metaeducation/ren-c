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

// Recursive panic() can generate a very large spew of output until the stack
// overflows.  Stop reentrant panics (though it would be good to find the
// cases that do this and make them give more useful output.)
//
static bool already_panicking = false;


#if DEBUG_FANCY_PANIC

// Because dereferencing pointers in sensitive situations can crash, we don't
// want output buffered...make sure we see as much as we can before a crash.
//
#define Printf_Stderr(...) do { \
    fprintf(stderr, __VA_ARGS__); \
    fflush(stderr); \
} while (0)


//
//  Panic_Stub_Debug: C
//
// The goal of this routine is to progressively reveal as much diagnostic
// information about a Stub as possible.  Since the routine will ultimately
// crash anyway, it is okay if the diagnostics run code which might be
// risky in an unstable state...though it is ideal if it can run to the end
// so it can trigger Address Sanitizer or Valgrind's internal stack dump.
//
ATTRIBUTE_NO_RETURN void Panic_Stub_Debug(const Stub* s)
{
    fflush(stdout);
    fflush(stderr);

    if (Is_Node_Managed(s))
        Printf_Stderr("managed");
    else
        Printf_Stderr("unmanaged");
    Printf_Stderr(" Stub");

  #if DEBUG_STUB_ORIGINS
    #if TRAMPOLINE_COUNTS_TICKS
        Printf_Stderr(" was likely ");
        if (Not_Node_Readable(s))
            Printf_Stderr("freed");
        else
            Printf_Stderr("created");
        Printf_Stderr(
            " during evaluator tick: %lu\n", cast(unsigned long, s->tick)
        );
    #else
      Printf_Stderr(" has no tick tracking (see TRAMPOLINE_COUNTS_TICKS)\n");
    #endif

    if (*s->guard == FREE_POOLUNIT_BYTE)  // should make valgrind or asan alert
        NOOP;

    Printf_Stderr(
        "Flex guard didn't trigger ASAN/Valgrind alert\n" \
        "Either not a Stub, not built with ASAN, or not running Valgrind\n"
    );
  #else
    Printf_Stderr("DEBUG_STUB_ORIGINS not enabled, no more info");
  #endif

    abort();
}


//
//  Panic_Cell_Debug: C
//
// This is a debug-only "error generator", which will hunt through all the
// Stub allocations and panic on the Stub or Array that contains the value (if
// it can find it).  This will allow those using Address Sanitizer or
// Valgrind to know a bit more about where the value came from.
//
// Additionally, it can dump out where the initialization happened if that
// information was stored.  See DEBUG_TRACK_EXTEND_CELLS.
//
ATTRIBUTE_NO_RETURN void Panic_Cell_Debug(const Cell* c) {
  #if DEBUG_TRACK_EXTEND_CELLS
    Printf_Stderr("Cell init");

    Printf_Stderr(" @ tick #%d", cast(unsigned int, c->tick));
    if (c->touch != 0)
        Printf_Stderr(" @ touch #%d", cast(unsigned int, c->touch));

    Printf_Stderr(" @ %s:%ld\n", c->file, cast(unsigned long, c->line));
  #else
    Printf_Stderr("No Cell track info (see DEBUG_TRACK_EXTEND_CELLS)\n");
  #endif

    Heart heart = Cell_Heart(c);
    const char *type = String_UTF8(Canon_Symbol(SYM_FROM_KIND(heart)));
    Printf_Stderr("cell_heart=%s\n", type);
    Printf_Stderr("quote_byte=%d\n", QUOTE_BYTE(c));

    if (Cell_Has_Node1(c))
        Printf_Stderr("has node1: %p\n", cast(void*, Cell_Node1(c)));
    if (Cell_Has_Node2(c))
        Printf_Stderr("has node2: %p\n", cast(void*, Cell_Node2(c)));

    Node* containing = Try_Find_Containing_Node_Debug(c);

    if (not containing) {
        Printf_Stderr("No containing Stub or Pairing (global variable?)\n");
        if (Cell_Has_Node1(c) and Is_Node_A_Stub(Cell_Node1(c))) {
            Printf_Stderr("Panicking node1 in case it helps\n");
            Panic_Stub_Debug(cast(Stub*, Cell_Node1(c)));
        }
        if (Cell_Has_Node2(c) and Is_Node_A_Stub(Cell_Node2(c))) {
            Printf_Stderr("No node1, panicking node2 in case it helps\n");
            Panic_Stub_Debug(cast(Stub*, Cell_Node2(c)));
        }
        Printf_Stderr("No node1 or node2 for further info, aborting\n");
        abort();
    }

    if (Is_Node_A_Stub(containing))
        Printf_Stderr("Containing Stub");
    else
        Printf_Stderr("Containing Pairing");
    Printf_Stderr("for value pointer found, %p:\n", cast(void*, containing));

    if (Is_Node_A_Stub(containing)) {
        Printf_Stderr("Panicking the Stub containing the Cell...\n");
        Panic_Stub_Debug(cast(Stub*, containing));
    }

    Printf_Stderr("Cell is (probably) first element of a Pairing\n");
    Printf_Stderr("Trying to panic its paired cell...\n");
    Panic_Cell_Debug(c + 1);
}

#endif  // DEBUG_FANCY_PANIC


//
//  Panic_Core: C
//
// Abnormal termination of Rebol.  The checked build is designed to present
// as much diagnostic information as it can on the passed-in pointer, which
// includes where a Flex* was allocated or freed.  Or if a Value* is
// passed in it tries to say what tick it was initialized on and what Array
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
    const void *p,  // Flex*, Value*, or UTF-8 char*
    Tick tick,
    const char *file, // UTF8
    int line
){
    g_gc.disabled = true;  // crashing is a legitimate reason to disable the GC

  #if DEBUG_FANCY_PANIC
    Printf_Stderr("C Source File %s, Line %d, Pointer %p\n", file, line, p);
    Printf_Stderr("At evaluator tick: %lu\n", cast(unsigned long, tick));

    fflush(stdout);  // release builds don't use <stdio.h>, but debug ones do
    fflush(stderr);  // ...so be helpful and flush any lingering debug output
  #else
    UNUSED(tick);
    UNUSED(file);
    UNUSED(line);
  #endif

    if (already_panicking) {
      #if DEBUG_FANCY_PANIC
        Printf_Stderr("!!! RECURSIVE PANIC, EXITING BEFORE IT GOES NUTS !!!\n");
      #endif
        abort();
    }
    already_panicking = true;

    // Delivering a panic should not rely on printf()/etc. in release build.

    char buf[PANIC_BUF_SIZE + 1];
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
            c_cast(char*, p),
            PANIC_BUF_SIZE - strsize(buf)
        );
        break;

      case DETECTED_AS_STUB: {  // non-FREE stub
      #if DEBUG_FANCY_PANIC
        const Stub* s = c_cast(Stub*, p);
        Printf_Stderr("Stub detected...\n");
        if (FLAVOR_BYTE(s) == FLAVOR_VARLIST) {
            Printf_Stderr("...and it's a varlist...\n");
            if (CTX_TYPE(x_cast(VarList*, s)) == REB_ERROR) {
                Printf_Stderr("...and it's an Error, trying to PROBE...\n");
                PROBE(s);  // this may crash recursively if it's corrupt
            }
        }
        Panic_Stub_Debug(s);
      #else
        strncat(buf, "non-free Stub", PANIC_BUF_SIZE - strsize(buf));
      #endif
        break; }

      case DETECTED_AS_CELL:
      case DETECTED_AS_END: {
      #if DEBUG_FANCY_PANIC
        const Cell* c = c_cast(Cell*, p);
        if (HEART_BYTE(c) == REB_ERROR) {
            Printf_Stderr("...panic on an ERROR! Cell, trying to PROBE...");
            PROBE(c);
        }
        Panic_Cell_Debug(c);
      #else
        strncat(buf, "value", PANIC_BUF_SIZE - strsize(buf));
      #endif
        break; }

      case DETECTED_AS_FREE:
        strncat(
            buf,
            "Panic was passed a likely freed PoolUnit",
            PANIC_BUF_SIZE - strsize(buf)
        );
      #if DEBUG_FANCY_PANIC
        Panic_Stub_Debug(u_cast(const Stub*, p));
      #endif
        break;
    }

  #if DEBUG_FANCY_PANIC
    Printf_Stderr("%s\n", Str_Panic_Title);
    Printf_Stderr("%s\n", buf);
  #else
    // How to report panic conditions in builds with no printf() linked?
    UNUSED(Str_Panic_Title);
    UNUSED(buf);
  #endif

  #if RUNTIME_CHECKS
    //
    // Note: Emscripten actually gives a more informative stack trace in
    // its checked build through plain exit().  It has DEBUG_FANCY_PANIC but
    // also defines NDEBUG to turn off RUNTIME_CHECKS.
    //
    debug_break();  // try to hook up to a C debugger - see %debug_break.h
  #endif

  #if DEBUG_FANCY_PANIC
    Printf_Stderr("debug_break() didn't terminate in panic()\n");
  #endif

    abort();
}


//
//  /panic: native [
//
//  "Terminate abnormally with a message, optionally diagnosing a value cell"
//
//      return: []
//      reason "Cause of the panic"
//          [any-value?]
//      :value "Interpret reason as a value cell to debug dump, vs. a message"
//  ]
//
DECLARE_NATIVE(panic)
{
    INCLUDE_PARAMS_OF_PANIC;

    Value* v = ARG(reason);  // remove quote level from @reason

  #if TRAMPOLINE_COUNTS_TICKS
    Tick tick = level_->tick;  // use Level's tick instead of g_ts.tick
  #else
    Tick tick = 0;
  #endif

    // panic() on the string value itself will report information about the
    // string cell...but panic() on UTF-8 character data assumes you mean to
    // report the contained message.  PANIC:VALUE for the latter intent.

    const void *p;

    if (REF(value)) {  // interpret reason as value to diagnose
        p = v;
    }
    else {  // interpret reason as a message
      if (Is_Keyword(v)) {
            p = String_UTF8(Cell_Word_Symbol(v));
        }
        else if (Is_Text(v)) {
            p = Cell_Utf8_At(v);
        }
        else if (Is_Error(v)) {
            p = Cell_Varlist(v);
        }
        else {
            assert(!"Called PANIC without :VALUE on non-TEXT!, non-ERROR!");
            p = v;
        }
    }

    Panic_Core(p, tick, File_UTF8_Of_Level(level_), LineNumber_Of_Level(level_));
}


//
//  /raise*: native [
//
//  "Version of RAISE of definitional error that only takes ERROR!"
//
//      return: [raised?]
//      reason [error!]
//  ]
//
DECLARE_NATIVE(raise_p)
{
    INCLUDE_PARAMS_OF_RAISE_P;

    Value* v = ARG(reason);

    return Raisify(COPY(v));
}


//
//  /fail: native [
//
//  "Early-boot version of FAIL (overridden by more complex usermode version)"
//
//      return: []
//      reason [any-value?]  ; permissive to avoid callsite error
//      :blame [word!]
//  ]
//
DECLARE_NATIVE(fail)
{
    INCLUDE_PARAMS_OF_FAIL;

    Value* reason = ARG(reason);
    Value* blame = ARG(blame);

  #if NO_RUNTIME_CHECKS
    UNUSED(blame);
  #else
    printf("!!! Early-Boot FAIL, e.g. /fail: native [], not /fail: func []\n");
    PROBE(blame);

    rebElide(Canon(WRITE_STDOUT), Canon(DELIMIT), Canon(SPACE), reason);
  #endif

    panic (reason);
}


#if DEBUG_CELL_READ_WRITE

//
//  Panic_Cell_Unreadable: C
//
// Only called when Assert_Cell_Readable() fails, no reason to inline it.
//
void Panic_Cell_Unreadable(const Cell* c) {
    if (not Is_Node(c))
        printf("Non-node passed to cell read routine\n");
    else if (not Is_Node_A_Cell(c))
        printf("Non-cell passed to cell read routine\n");
    else {
        assert(Not_Node_Readable(c));
        printf("Assert_Cell_Readable() on NODE_FLAG_UNREADABLE cell\n");
    }
    panic (c);
}


//
//  Panic_Cell_Unwritable: C
//
// Only called when Assert_Cell_Writable() fails, no reason to inline it.
//
void Panic_Cell_Unwritable(Cell* c) {
    if (not Is_Node(c))
        printf("Non-node passed to cell write routine\n");
    else if (not Is_Node_A_Cell(c))
        printf("Non-cell passed to cell write routine\n");
    else {
        assert(Get_Cell_Flag(c, PROTECTED));
        printf("Protected cell passed to writing routine\n");
    }
    panic (c);
}

#endif


#if CHECK_MEMORY_ALIGNMENT

//
//  Panic_Cell_Unaligned: C
//
// Only called when Assert_Cell_Aligned() fails, no reason to inline it.
//
void Panic_Cell_Unaligned(Cell* c) {
    printf(
        "Cell address %p not aligned to %d bytes\n",
        c_cast(void*, (c)),
        cast(int, ALIGN_SIZE)
    );
    panic (c);
}

#endif
