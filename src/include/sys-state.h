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
**  Summary: CPU State
**  Module:  sys-state.h
**  Author:  Carl Sassenrath, @HostileFork
**  Notes:
**      Rebol is settled upon a stable and pervasive implementation
**      baseline of ANSI-C (C89).  That commitment provides certain
**      advantages.  One of the disadvantages is that there is no safe
**      way to do non-local jumps with stack unwinding (as in C++).  If
**      you've written some code that performs a raw malloc and then
**      wants to "throw" with a longjmp, that will leak the malloc.
**
**      Basically all code must be aware of "throws", and if one can
**      happen then there must be explicit handling of the cleanup.
**      This must be either at the point of the longjmp, or the moment
**      when the setjmp runs its "true" branch after a non-local jump:
**
**          http://stackoverflow.com/questions/1376085/
**
**      (Note:  If you are integrating with C++ and a longjmp crosses
**      a constructed object, abandon all hope...UNLESS you use Ren/C++.
**      It is careful to avoid this trap, and you don't want to redo
**      that work.)
**
**      !!! v-- TRIAGE NOT YET INTEGRATED IN REN/C
**
**      In order to mitigate the inherent failure of trying to emulate
**      stack unwinding via longjmp, Rebol wraps the abstraction a bit.
**      If you had allocated any series and they were in "triage" at
**      the time the "throw" happened, then those will be automatically
**      freed (the garbage collector didn't know about them yet).
**
***********************************************************************/


// "Under FreeBSD 5.2.1 and Mac OS X 10.3, setjmp and longjmp save and restore
// the signal mask. Linux 2.4.22 and Solaris 9, however, do not do this.
// FreeBSD and Mac OS X provide the functions _setjmp and _longjmp, which do
// not save and restore the signal mask."
//
// "To allow either form of behavior, POSIX.1 does not specify the effect of
// setjmp and longjmp on signal masks. Instead, two new functions, sigsetjmp
// and siglongjmp, are defined by POSIX.1. These two functions should always
// be used when branching from a signal handler."
//
// Note: longjmp is able to pass a value (though only an integer on 64-bit
// platforms, and not enough to pass a pointer).  This can be used to
// dictate the value setjmp returns in the longjmp case, though the code
// does not currently use that feature.
//
// Also note: with compiler warnings on, it can tell us when values are set
// before the setjmp and then changed before a potential longjmp:
//
//     http://stackoverflow.com/q/7721854/211160
//
// Because of this longjmp/setjmp "clobbering", it's a useful warning to
// have enabled in.  One option for suppressing it would be to mark
// a parameter as 'volatile', but that is implementation-defined.
// It is best to use a new variable if you encounter such a warning.

#ifdef HAS_POSIX_SIGNAL
    #define SET_JUMP(s) sigsetjmp((s), 1)
    #define LONG_JUMP(s, v) siglongjmp((s), (v))
#else
    #define SET_JUMP(s) setjmp(s)
    #define LONG_JUMP(s, v) longjmp((s), (v))
#endif


// Structure holding the information about the last point in the stack that
// wanted to set up an opportunity to intercept a `fail (Error_XXX())`

typedef struct Rebol_State {
    struct Rebol_State *last_state;

    REBINT dsp;
    struct Reb_Call *dsf;
    REBCNT series_guard_tail;
    REBCNT value_guard_tail;
    REBSER *error_frame;
    REBINT gc_disable;      // Count of GC_Disables at time of Push

    REBCNT manuals_tail;    // Where GC_Manuals was when state started

#ifdef HAS_POSIX_SIGNAL
    sigjmp_buf cpu_state;
#else
    jmp_buf cpu_state;
#endif
} REBOL_STATE;


// PUSH_TRAP is a construct which is used to catch errors that have been
// triggered by the Fail_Core() function.  This can be triggered by a usage
// of the `fail` pseudo-"keyword" in C code, and in Rebol user code by the
// REBNATIVE(fail).  To call the push, you need a REBOL_STATE value to be
// passed which it will write into--which is a black box that clients
// shouldn't inspect.
//
// The routine also takes a pointer-to-a-REBSER-pointer which represents
// an error.  Using the tricky mechanisms of setjmp/longjmp, there will
// be a first pass of execution where the line of code after the PUSH_TRAP
// will see the error pointer as being NULL.  If a trap occurs during
// code before the paired DROP_TRY happens, then the C state will be
// magically teleported back to the line after the PUSH_TRAP with the
// error value now non-null and usable, including put into a REBVAL via
// the `Val_Init_Error()` function.
//
// Note: The implementation of this macro was chosen stylistically to
// hide the result of the setjmp call.  That's because you really can't
// put "setjmp" in arbitrary conditions like `setjmp(...) ? x : y`.  That's
// against the rules.  So although the preprocessor abuse below is a bit
// ugly, it helps establish that anyone modifying this code later not be
// able to avoid the truth of the limitation:
//
//      http://stackoverflow.com/questions/30416403/

#define PUSH_TRAP(e,s) \
    do { \
        Push_Trap_Helper(s); \
        assert((s)->last_state != NULL); /* top push MUST handle halts */ \
        if (!SET_JUMP((s)->cpu_state)) { \
            /* this branch will always be run */ \
            *(e) = NULL; \
        } else { \
            /* this runs if before the DROP_TRAP a longjmp() happens */ \
            if (Trapped_Helper_Halted(s)) \
                fail ((s)->error_frame); /* proxy the halt up the stack */ \
            else \
                *(e) = (s)->error_frame; \
        } \
    } while (0)


// PUSH_UNHALTABLE_TRAP is a form of PUSH_TRAP that will receive RE_HALT in
// the same way it would be told about other errors.  In a pure C client,
// it would usually be only at the topmost level (e.g. console REPL loop).
//
// It's also necessary at C-to-C++ boundary crossings (as in Ren/C++) even
// if they are not the topmost.  This is because C++ needs to know if *any*
// longjmp happens, to keep it from crossing stack frames with constructed
// objects without running their destructors.  Once it is done unwinding
// any relevant C++ call frames, it may have to trigger another longjmp IF
// the C++ code was called from other Rebol C code.  (This is done in the
// exception handler found in Ren/C++'s %function.hpp)
//
// Note: Despite the technical needs of low-level clients, there is likely
// no reasonable use-case for a user-exposed ability to intercept HALTs in
// Rebol code, for instance with a "TRY/HALTABLE" construction.

#define PUSH_UNHALTABLE_TRAP(e,s) \
    do { \
        Push_Trap_Helper(s); \
        if (!SET_JUMP((s)->cpu_state)) { \
            /* this branch will always be run */ \
            *(e) = NULL; \
        } else { \
            /* this runs if before the DROP_TRAP a longjmp() happens */ \
            cast(void, Trapped_Helper_Halted(s)); \
            *(e) = (s)->error_frame; \
        } \
    } while (0)


// If either a haltable or non-haltable TRY is PUSHed, it must be DROP'd.
// DROP_TRAP_SAME_STACKLEVEL_AS_PUSH has a long and informative name to
// remind you that you must DROP_TRY from the same scope you PUSH_TRAP
// from.  (So do not call PUSH_TRAP in a function, then return from that
// function and DROP_TRY at another stack level.)
//
//      "If the function that called setjmp has exited (whether by return
//      or by a different longjmp higher up the stack), the behavior is
//      undefined. In other words, only long jumps up the call stack
//      are allowed."
//
//      http://en.cppreference.com/w/c/program/longjmp

#define DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(s) \
    do { \
        assert(GC_Series_Guard->tail == (s)->series_guard_tail); \
        assert(GC_Value_Guard->tail == (s)->value_guard_tail); \
        assert(GC_Disabled == (s)->gc_disable); \
        assert(!(s)->error_frame); \
        Saved_State = (s)->last_state; \
    } while (0)
