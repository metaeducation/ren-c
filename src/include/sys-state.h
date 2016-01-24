//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2015 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
//  Summary: CPU and Interpreter State Snapshot/Restore
//  File: %sys-state.h
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol is settled upon a stable and pervasive implementation baseline of
// ANSI-C (C89).  That commitment provides certain advantages.
//
// One of the *disadvantages* is that there is no safe way to do non-local
// jumps with stack unwinding (as in C++).  If you've written some code that
// performs a raw malloc and then wants to "throw" via a `longjmp()`, that
// will leak the malloc.
//
// In order to mitigate the inherent failure of trying to emulate stack
// unwinding via longjmp, the macros in this file provide an abstraction
// layer.  These allow Rebol to clean up after itself for some kinds of
// "dangling" state--such as manually memory managed series that have been
// made with Make_Series() but never passed to either Free_Series() or
// MANAGE_SERIES().  This covers several potential leaks known-to-Rebol,
// but custom interception code is needed for any generalized resource
// that might be leaked in the case of a longjmp().
//
// The triggering of the longjmp() is done via "fail", and it's important
// to know the distinction between a "fail" and a "throw".  In Rebol
// terminology, a `throw` is a cooperative concept, which does *not* use
// longjmp(), and instead must cleanly pipe the thrown value up through
// the OUT pointer that each function call writes into.  The `throw` will
// climb the stack until somewhere in the backtrace, one of the calls
// chooses to intercept the thrown value instead of pass it on.
//
// By contrast, a `fail` is non-local control that interrupts the stack,
// and can only be intercepted by points up the stack that have explicitly
// registered themselves interested.  So comparing these two bits of code:
//
//     catch [if 1 < 2 [trap [print ["Foo" (throw "Throwing")]]]]
//
//     trap [if 1 < 2 [catch [print ["Foo" (fail "Failing")]]]]
//
// In the first case, the THROW is offered to each point up the chain as
// a special sort of "return value" that only natives can examine.  The
// `print` will get a chance, the `trap` will get a chance, the `if` will
// get a chance...but only CATCH will take the opportunity.
//
// In the second case, the FAIL is implemented with longjmp().  So it
// doesn't make a return value...it never reaches the return.  It offers an
// ERROR! up the stack to native functions that have called PUSH_TRAP() in
// advance--as a way of registering interest in intercepting failures.  For
// IF or CATCH or PRINT to have an opportunity, they would need to be chang
// d to include a PUSH_TRAP() call.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTE: If you are integrating with C++ and a longjmp crosses a constructed
// object, abandon all hope...UNLESS you use Ren-cpp.  It is careful to
// avoid this trap, and you don't want to redo that work.
//
//     http://stackoverflow.com/questions/1376085/
//


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
//
#ifdef HAS_POSIX_SIGNAL
    #define SET_JUMP(s) sigsetjmp((s), 1)
    #define LONG_JUMP(s, v) siglongjmp((s), (v))
#else
    #define SET_JUMP(s) setjmp(s)
    #define LONG_JUMP(s, v) longjmp((s), (v))
#endif


// Structure holding the information about the last point in the stack that
// wanted to set up an opportunity to intercept a `fail (Error_XXX())`

struct Reb_State {
    struct Reb_State *last_state;

    REBDSP dsp;
    struct Reb_Chunk *top_chunk;
    struct Reb_Call *call;
    REBCNT series_guard_len;
    REBCNT value_guard_len;
    struct Reb_Call *do_stack; // is it necessary to keep this *and* DSF?
    REBCTX *error;
    REBINT gc_disable;      // Count of GC_Disables at time of Push

    REBCNT manuals_len;    // Where GC_Manuals was when state started
    REBCNT uni_buf_len;
    REBCNT mold_loop_tail;

#ifdef HAS_POSIX_SIGNAL
    sigjmp_buf cpu_state;
#else
    jmp_buf cpu_state;
#endif
};


// SNAP_STATE will record the interpreter state but not include it into
// the chain of trapping points.  This is used by PUSH_TRAP but also by
// debug code that just wants to record the state to make sure it balances
// back to where it was.
//
#define SNAP_STATE(s) \
    Snap_State_Core(s)


// PUSH_TRAP is a construct which is used to catch errors that have been
// triggered by the Fail_Core() function.  This can be triggered by a usage
// of the `fail` pseudo-"keyword" in C code, and in Rebol user code by the
// REBNATIVE(fail).  To call the push, you need a `struct Reb_State` to be
// passed which it will write into--which is a black box that clients
// shouldn't inspect.
//
// The routine also takes a pointer-to-a-REBCTX-pointer which represents
// an error.  Using the tricky mechanisms of setjmp/longjmp, there will
// be a first pass of execution where the line of code after the PUSH_TRAP
// will see the error pointer as being NULL.  If a trap occurs during
// code before the paired DROP_TRAP happens, then the C state will be
// magically teleported back to the line after the PUSH_TRAP with the
// error value now non-null and usable, including put into a REBVAL via
// the `Val_Init_Error()` function.
//
#define PUSH_TRAP(e,s) \
    PUSH_TRAP_CORE((e), (s), TRUE)


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
// Rebol code, for instance with a "TRAP/HALT" construction.
//
#define PUSH_UNHALTABLE_TRAP(e,s) \
    PUSH_TRAP_CORE((e), (s), FALSE)


// Core implementation behind PUSH_TRAP and PUSH_UNHALTABLE_TRAP.
//
// Note: The implementation of this macro was chosen stylistically to
// hide the result of the setjmp call.  That's because you really can't
// put "setjmp" in arbitrary conditions like `setjmp(...) ? x : y`.  That's
// against the rules.  So although the preprocessor abuse below is a bit
// ugly, it helps establish that anyone modifying this code later not be
// able to avoid the truth of the limitation:
//
//      http://stackoverflow.com/questions/30416403/
//
#define PUSH_TRAP_CORE(e,s,haltable) \
    do { \
        assert(Saved_State || (DSP == 0 && !DSF)); \
        Snap_State_Core(s); \
        (s)->last_state = Saved_State; \
        Saved_State = (s); \
        if (haltable) { \
            /* the topmost TRAP must be PUSH_UNHALTABLE_TRAP */ \
            assert((s)->last_state != NULL); \
        } \
        if (!SET_JUMP((s)->cpu_state)) { \
            /* this branch will always be run */ \
            *(e) = NULL; \
        } \
        else { \
            /* this runs if before the DROP_TRAP a longjmp() happens */ \
            if (haltable) { \
               if (Trapped_Helper_Halted(s)) \
                    fail ((s)->error); /* proxy the halt up the stack */ \
                else \
                    *(e) = (s)->error; \
            } \
            else { \
               cast(void, Trapped_Helper_Halted(s)); \
                *(e) = (s)->error; \
            } \
        } \
    } while (0)


// If either a haltable or non-haltable TRAP is PUSHed, it must be DROP'd.
// DROP_TRAP_SAME_STACKLEVEL_AS_PUSH has a long and informative name to
// remind you that you must DROP_TRAP from the same scope you PUSH_TRAP
// from.  (So do not call PUSH_TRAP in a function, then return from that
// function and DROP_TRAP at another stack level.)
//
//      "If the function that called setjmp has exited (whether by return
//      or by a different longjmp higher up the stack), the behavior is
//      undefined. In other words, only long jumps up the call stack
//      are allowed."
//
//      http://en.cppreference.com/w/c/program/longjmp
//
// Note: There used to be more aggressive balancing-oriented asserts, making
// this a point where outstanding manuals or guarded values and series would
// have to be balanced.  Those seemed to be more irritating than helpful,
// so the asserts have been left to the evaluator's bracketing.
//
#define DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(s) \
    do { \
        assert(!(s)->error); \
        Saved_State = (s)->last_state; \
    } while (0)


// ASSERT_STATE_BALANCED is used to check that the situation modeled in a
// SNAP_STATE has balanced out, without a trap (e.g. it is checked each time
// the evaluator completes a cycle in the debug build)
//
#ifdef NDEBUG
    #define ASSERT_STATE_BALANCED(s) NOOP
#else
    #define ASSERT_STATE_BALANCED(s) \
        Assert_State_Balanced_Debug((s), __FILE__, __LINE__)
#endif
