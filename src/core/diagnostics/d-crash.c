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
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// Abnormal termination of Rebol.  The checked build is designed to present
// as much diagnostic information as it can on the passed-in pointer, which
// includes where a Flex* was allocated or freed.  Or if a Stable* is
// passed in it tries to say what tick it was initialized on and what Array
// it lives in.  If the pointer is a simple UTF-8 string pointer, then that
// is delivered as a message.
//
// This can be triggered via the macros crash() and crash_at(), which are
// unsalvageable situations in the core code.  It can also be triggered by
// the CRASH native, and since it can be hijacked that offers hookability for
// "recoverable" forms of CRASH.
//

#include "sys-core.h"


// Size of crash buffers
#define PANIC_BUF_SIZE 512


// Recursive crash() can generate a very large spew of output until the stack
// overflows.  Stop reentrant crashes (though it would be good to find the
// cases that do this and make them give more useful output.)
//
static bool g_already_crashing = false;


#if DEBUG_FANCY_CRASH

// Rather than simply calling abort(), we try to get as much diagnostic info
// as possible.  This includes stack traces if available...or breaking into a
// C integrated debugger if one is attached.
//
static ATTRIBUTE_NO_RETURN void Crash_Of_Last_Resort(void)
{
    Printf_Stderr("\n\n=== !!! Crash_Of_Last_Resort() !!! ===\n");

  print_stack_trace: {

  // 1. Emscripten actually gives an informative stack trace through a plain
  //    abort()...BUT you have to build with `-g` and `-s ASSERTIONS=1`.
  //
  // 2. If Address Sanitizer is enabled, it's preferable to dereference a bad
  //    pointer to crash.  This should trigger ASAN to print out a useful stack
  //    trace with symbols and line numbers of each frame.
  //
  // 3. A crappier stack trace may be possible even without ASAN.  But see the
  //    caveats in the comments on HAVE_EXECINFO_H_AVAILABLE about how bad an
  //    idea it is to build without `-fvisibility=hidden` to get this feature
  //    on Unix-like systems.

  #if TO_EMSCRIPTEN
    Printf_Stderr("Emscripten build: just abort() to get the stack trace...");
    abort();  // shows trace in checked build [1]
  #elif defined(__SANITIZE_ADDRESS__)
    Printf_Stderr("Dereferencing bad pointer for a backtrace from ASAN...\n");
    volatile int *bad_ptr = p_cast(int*, 0x1);
    *bad_ptr = 42;  // crash, but get a good stack trace [2]
    Printf_Stderr("...dereference didn't crash, likely no trace showed.\n\n");
  #else
    Print_C_Stack_Trace_If_Available();  // try crappier stack trace [3]
  #endif

} try_breaking_into_c_debugger: {

  // There's no easy way to detect if you're running under a debugger, but if
  // you are then a debug_break() call should get you the ability to inspect
  // the stack and variables at this point.

  #if RUNTIME_CHECKS
    Printf_Stderr("Trying a debug_break() to hook up to a C debugger...\n");
    debug_break();  // see %debug_break.h
    Printf_Stderr("...but debug_break() didn't terminate in crash()\n\n");
  #endif

} give_up_and_abort: {

    Printf_Stderr("Oh well.  We give up.  Just calling abort()\n");
    abort();
}}


//
//  Crash_With_Stub_Debug: C
//
// The goal of this routine is to progressively reveal as much diagnostic
// information about a Stub as possible.  Since the routine will ultimately
// crash anyway, it is okay if the diagnostics run code which might be
// risky in an unstable state...though it is ideal if it can run to the end
// so it can trigger Address Sanitizer or Valgrind's internal stack dump.
//
ATTRIBUTE_NO_RETURN void Crash_With_Stub_Debug(const Stub* s)
{
    fflush(stdout);
    fflush(stderr);

    if (Is_Base_Managed(s))
        Printf_Stderr("managed");
    else
        Printf_Stderr("unmanaged");
    Printf_Stderr(" Stub\n");

  #if DEBUG_STUB_ORIGINS
    #if TRAMPOLINE_COUNTS_TICKS
        Printf_Stderr(" was likely ");
        if (Not_Base_Readable(s))
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

    Crash_Of_Last_Resort();
}


//
//  Crash_With_Cell_Debug: C
//
// This is a debug-only "error generator", which will hunt through all the
// Stub allocations and crash on the Pairing or Array that contains the value
// (if it can find it).  This will allow those using Address Sanitizer or
// Valgrind to know a bit more about where the value came from.
//
// Additionally, it can dump out where the initialization happened if that
// information was stored.  See DEBUG_TRACK_EXTEND_CELLS.
//
ATTRIBUTE_NO_RETURN void Crash_With_Cell_Debug(const Cell* c) {
  #if DEBUG_TRACK_EXTEND_CELLS
    Printf_Stderr("Cell init");

    Printf_Stderr(" @ tick #%d", cast(unsigned int, c->tick));
    if (c->touch != 0)
        Printf_Stderr(" @ touch #%d", cast(unsigned int, c->touch));

    Printf_Stderr(" @ %s:%ld\n", c->file, cast(unsigned long, c->line));
  #else
    Printf_Stderr("No Cell track info (see DEBUG_TRACK_EXTEND_CELLS)\n");
  #endif

    Option(Heart) heart = Heart_Of(c);
    Option(SymId) id = heart ? Symbol_Id_From_Type(unwrap heart) : SYM_0;
    const char *name = id ? Strand_Utf8(Canon_Symbol(unwrap id)) : "custom-0";
    Printf_Stderr("Cell.kind_byte=%d\n", u_cast(int, KIND_BYTE(c)));
    Printf_Stderr("cell heart name=%s\n", name);
    Printf_Stderr("Cell.lift_byte=%d\n", u_cast(int, LIFT_BYTE(c)));

    if (Cell_Payload_1_Needs_Mark(c))
        Printf_Stderr("has payload1: %p\n", cast(void*, CELL_PAYLOAD_1(c)));
    if (Cell_Payload_2_Needs_Mark(c))
        Printf_Stderr("has payload2: %p\n", cast(void*, CELL_PAYLOAD_2(c)));

    Base* containing = Try_Find_Containing_Base_Debug(c);

    if (not containing) {
        Printf_Stderr("No containing Stub or Pairing (global variable?)\n");
        if (Cell_Payload_1_Needs_Mark(c) and Is_Base_A_Stub(CELL_PAYLOAD_1(c))) {
            Printf_Stderr("Crashing on payload1 in case it helps\n");
            Crash_With_Stub_Debug(cast(Stub*, CELL_PAYLOAD_1(c)));
        }
        if (Cell_Payload_2_Needs_Mark(c) and Is_Base_A_Stub(CELL_PAYLOAD_2(c))) {
            Printf_Stderr("No payload1, crashing on payload2 in case it helps\n");
            Crash_With_Stub_Debug(cast(Stub*, CELL_PAYLOAD_2(c)));
        }
        Printf_Stderr("No payload1 or payload2 for further info, aborting\n");
        Crash_Of_Last_Resort();
    }

    if (Is_Base_A_Stub(containing))
        Printf_Stderr("Containing Stub");
    else
        Printf_Stderr("Containing Pairing");
    Printf_Stderr("for value pointer found, %p:\n", cast(void*, containing));

    if (Is_Base_A_Stub(containing)) {
        Printf_Stderr("Panicking the Stub containing the Cell...\n");
        Crash_With_Stub_Debug(cast(Stub*, containing));
    }

    Printf_Stderr("Cell is (probably) first element of a Pairing\n");
    Printf_Stderr("Trying to crash on its paired cell...\n");
    Crash_With_Cell_Debug(c + 1);
}

#else  // e.g. (! DEBUG_FANCY_CRASH)

static ATTRIBUTE_NO_RETURN void Crash_Of_Last_Resort(void) {
    abort();
}

#endif  // DEBUG_FANCY_CRASH


//
//  Crash_Core: C
//
// Main entry point for crash handling.  Detects what kind of pointer was
// passed to crash() or crash_at().
//
// coverity[+kill]
//
ATTRIBUTE_NO_RETURN void Crash_Core(
    Option(const void *) p,  // Flex*, Stable*, or UTF-8 char*
    Tick tick,
    const char *file, // UTF8
    int line
){
  #if RUNTIME_CHECKS
    fflush(stdout);  // release builds don't use <stdio.h>, but debug ones do

    Emergency_Shutdown_Gc_Debug();
  #endif

  #if DEBUG_FANCY_CRASH
    Printf_Stderr(
        "C Source File %s, Line %d, Pointer %p\n",
        file,
        line,
        unwrap p
    );
    Printf_Stderr("At evaluator tick: %lu\n", cast(unsigned long, tick));
  #else
    UNUSED(tick);
    UNUSED(file);
    UNUSED(line);
  #endif

    if (g_already_crashing) {
      #if DEBUG_FANCY_CRASH
        Printf_Stderr("!!! RECURSIVE PANIC, EXITING BEFORE IT GOES NUTS !!!\n");
      #endif
        Crash_Of_Last_Resort();
    }
    g_already_crashing = true;

    // Delivering a crash should not rely on printf()/etc. in release build.

    char buf[PANIC_BUF_SIZE + 1];
    buf[0] = '\0';

  #if RUNTIME_CHECKS && 0  // disabled unless a need arises
    //
    // Showing the interpreter stack used to be kind of superfluous, because
    // you could tell what was going on from the C stack.  But now that things
    // are stackless and using the trampoline, it's more useful.  Review.
    //
    Dump_Info();
    Dump_Interpreter_Stack(TOP_LEVEL, 0);
  #endif

  #if DEBUG_FANCY_CRASH && 0  // disabled unless a need arises
    //
    // See remarks in %d-backtrace.c about why HAVE_EXECINFO_H_AVAILABLE is
    // a bad idea, due to needing to turn of -fvisibility=hidden.  Generally
    // on Unixlike systems, you should use Address Sanitizer or Valgrind to
    // get better stack traces.  See Crash_Of_Last_Resort() for more details.
    //
    Print_C_Stack_Trace_If_Available();
  #endif

    strncat(buf, g_crash_directions, PANIC_BUF_SIZE - 0);
    strncat(buf, "\n", PANIC_BUF_SIZE - strsize(buf));

    if (not p) {
        strncat(
            buf,
            "Panic was passed C nullptr",
            PANIC_BUF_SIZE - strsize(buf)
        );
    }
    else switch (Detect_Rebol_Pointer(unwrap p)) {
      case DETECTED_AS_UTF8: // string might be empty...handle specially?
        strncat(
            buf,
            cast(char*, unwrap p),
            PANIC_BUF_SIZE - strsize(buf)
        );
        break;

      case DETECTED_AS_STUB: {  // non-FREE stub
      #if DEBUG_FANCY_CRASH
        const Stub* s = cast(Stub*, unwrap p);
        Printf_Stderr("Stub detected...\n");
        if (Stub_Flavor(s) == FLAVOR_VARLIST) {
            Printf_Stderr("...and it's a varlist...\n");
            if (CTX_TYPE(cast(VarList*, m_cast(Stub*, s))) == TYPE_WARNING) {
                Printf_Stderr("...and it's an Error, trying to PROBE...\n");
                PROBE(s);  // this may crash recursively if it's corrupt
            }
        }
        Crash_With_Stub_Debug(s);
      #else
        strncat(buf, "non-free Stub", PANIC_BUF_SIZE - strsize(buf));
      #endif
        break; }

      case DETECTED_AS_CELL:
      case DETECTED_AS_END: {
      #if DEBUG_FANCY_CRASH
        const Cell* c = cast(Cell*, unwrap p);
        if (Heart_Of(c) == TYPE_WARNING) {
            Printf_Stderr("...crash() on an ERROR! Cell, trying to PROBE...");
            PROBE(c);
        }
        Crash_With_Cell_Debug(c);
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
      #if DEBUG_FANCY_CRASH
        Crash_With_Stub_Debug(u_cast(const Stub*, unwrap p));
      #endif
        break;

      case DETECTED_AS_WILD:
        strncat(
            buf,
            "Panic was passed a wild pointer",
            PANIC_BUF_SIZE - strsize(buf)
        );
        break;
    }

  #if DEBUG_FANCY_CRASH
    Printf_Stderr("%s\n", g_crash_title);
    Printf_Stderr("%s\n", buf);
  #else
    // How to report crash conditions in builds with no printf() linked?
    UNUSED(g_crash_title);
    UNUSED(buf);
  #endif

    Crash_Of_Last_Resort();
}


//
//  crash: native [
//
//  "Terminate abnormally.  By design, do not allow any more user code to run."
//
//      return: [<divergent>]
//      @info "If you want to implicate a value, use (crash @value)"
//          [<end> warning! text! @word!]
//  ]
//
DECLARE_NATIVE(CRASH)
//
// We don't want to run any code that could potentially panic and derail the
// crashing intent.  So this should only inertly interpret whatever is passed.
// This could be a block specifying a table of variables to dump and extra
// information, but it's much simpler than that at the moment.
{
    INCLUDE_PARAMS_OF_CRASH;

    Stable* info = ARG(INFO);

  #if TRAMPOLINE_COUNTS_TICKS
    Tick tick = level_->tick;  // use Level's tick instead of g_tick
  #else
    Tick tick = 0;
  #endif

    const void *p;

    if (Is_Pinned_Form_Of(WORD, info)) {  // interpret as value to diagnose
        Api(Stable*) fetched = rebStable(CANON(GET), rebQ(info));
        Copy_Cell(info, fetched);
        rebRelease(fetched);
        p = info;
    }
    else {  // interpret reason as a message
        if (Is_Text(info)) {
            p = cast(char*, Cell_Utf8_At(info));
        }
        else if (Is_Warning(info)) {
            p = Cell_Varlist(info);
        }
        else {
            assert(!"Called CRASH on non-TEXT!, non-WARNING!, non @WORD!");
            p = info;
        }
    }

    Crash_Core(
        p, tick, File_UTF8_Of_Level(level_), opt Line_Number_Of_Level(level_)
    );
}


//
//  fail*: native [
//
//  "Version of FAIL of definitional error that only takes ERROR!"
//
//      return: [error!]
//      reason [warning!]
//  ]
//
DECLARE_NATIVE(FAIL_P)
{
    INCLUDE_PARAMS_OF_FAIL_P;

    Stable* v = ARG(REASON);

    Copy_Cell(OUT, v);
    return Failify(OUT);
}


//
//  panic: native [
//
//  "Early-boot version of panic (overridden by more complex usermode version)"
//
//      return: [<divergent>]
//      reason [any-stable?]  ; permissive to avoid callsite error
//      :blame [word!]
//  ]
//
DECLARE_NATIVE(PANIC)
{
    INCLUDE_PARAMS_OF_PANIC;

    Stable* reason = ARG(REASON);
    Stable* blame = opt ARG(BLAME);

  #if NO_RUNTIME_CHECKS
    UNUSED(blame);
  #else
    printf("!!! Early-Boot PANIC, e.g. panic: native [], not panic: func []\n");
    PROBE(blame);

    rebElide(CANON(WRITE_STDOUT), CANON(DELIMIT), CANON(SPACE), reason);
  #endif

    crash (reason);
}


#if DEBUG_CELL_READ_WRITE

//
//  Crash_On_Unreadable_Cell: C
//
// Only called when Assert_Cell_Readable() fails, no reason to inline it.
//
void Crash_On_Unreadable_Cell(const Cell* c) {
    if (not Is_Base(c))
        printf("Non-node passed to cell read routine\n");
    else if (not Is_Base_A_Cell(c))
        printf("Non-cell passed to cell read routine\n");
    else {
        assert(Not_Base_Readable(c));
        printf("Assert_Cell_Readable() on BASE_FLAG_UNREADABLE cell\n");
    }
    crash (c);
}


//
//  Crash_On_Unwritable_Cell: C
//
// Only called when Assert_Cell_Writable() fails, no reason to inline it.
//
void Crash_On_Unwritable_Cell(const Cell* c) {
    if (not Is_Base(c))
        printf("Non-Base passed to cell write routine\n");
    else if (not Is_Base_A_Cell(c))
        printf("Non-Cell passed to cell write routine\n");
    else {
        assert(Get_Cell_Flag(c, PROTECTED));
        printf("Protected Cell passed to writing routine\n");
    }
    crash (c);
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
        cast(void*, (c)),
        cast(int, ALIGN_SIZE)
    );
    crash (c);
}

#endif
