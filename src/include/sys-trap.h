//
//  File: %sys-trap.h
//  Summary: "CPU and Interpreter State Snapshot/Restore"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// made with Make_Ser() but never passed to either Free_Unmanaged_Series()
// or MANAGE_SERIES().  This covers several potential leaks known-to-Rebol,
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
// IF or CATCH or PRINT to have an opportunity, they would need to be changed
// to include a PUSH_TRAP() call.
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
#if defined(__MINGW64__) && (__GNUC__ < 5)
    //
    // 64-bit builds made by MinGW in the 4.x range have an unfortunate bug in
    // the setjmp/longjmp mechanic, which causes hangs for reasons that are
    // seemingly random, like "using -O0 optimizations instead of -O2":
    //
    // https://sourceforge.net/p/mingw-w64/bugs/406/
    //
    // Bending to the bugs of broken compilers is usually not interesting, but
    // the Travis CI cross-platform builds on Linux targeting Windows were set
    // up on this old version--which otherwise is a good test the codebase
    // hasn't picked up dependencies that are too "modern".

    #define SET_JUMP(s) \
        __builtin_setjmp(s)

    #define LONG_JUMP(s,v) \
        __builtin_longjmp((s), (v))

#elif defined(HAS_POSIX_SIGNAL)
    #define SET_JUMP(s) \
        sigsetjmp((s), 1)

    #define LONG_JUMP(s,v) \
        siglongjmp((s), (v))
#else
    #define SET_JUMP(s) \
        setjmp(s)

    #define LONG_JUMP(s,v) \
        longjmp((s), (v))
#endif


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
// error context now non-null and usable.
//
// Note: The implementation of this macro was chosen stylistically to
// hide the result of the setjmp call.  That's because you really can't
// put "setjmp" in arbitrary conditions like `setjmp(...) ? x : y`.  That's
// against the rules.  So although the preprocessor abuse below is a bit
// ugly, it helps establish that anyone modifying this code later not be
// able to avoid the truth of the limitation:
//
// http://stackoverflow.com/questions/30416403/
//
// !!! THIS CAN'T BE INLINED due to technical limitations of using setjmp()
// in inline functions (at least in gcc)
//
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=24556
//
// According to the developers, "This is not a bug as if you inline it, the
// place setjmp goes to could be not where you want to goto."
//
// !!! An assertion that you don't try to push a trap with no saved state
// unless FS_TOP == FS_BOTTOM is commented out for this moment, because a
// top level rebValue() currently executes and then runs a trap inside of it.
// The API model is still being worked out, and so this is tolerated while
// the code settles--until the right answer can be seen more clearly.
//
#define PUSH_TRAP(e,s) \
    do { \
        /* assert(Saved_State or (DSP == 0 and FS_TOP == FS_BOTTOM)); */ \
        if (Saved_State == NULL) \
            Set_Stack_Limit(s); \
        Snap_State_Core(s); \
        (s)->last_state = Saved_State; \
        Saved_State = (s); \
        if (!SET_JUMP((s)->cpu_state)) \
            *(e) = NULL; /* this branch will always be run */ \
        else { \
            Trapped_Helper(s); \
            *(e) = (s)->error; \
        } \
    } while (0)


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
inline static void DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(struct Reb_State *s) {
    assert(!s->error);
    Saved_State = s->last_state;
}


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


//
// FAIL
//
// The fail() macro implements a form of error which is "trappable" with the
// macros above:
//
//     if (Foo_Type(foo) == BAD_FOO) {
//         fail (Error_Bad_Foo_Operation(...));
//
//         /* this line will never be reached, because it
//            longjmp'd up the stack where execution continues */
//     }
//
// Errors that originate from C code are created via Make_Error, and are
// defined in %errors.r.  These definitions contain a formatted message
// template, showing how the arguments will be displayed in FORMing.
//
// NOTE: It's desired that there be a space in `fail (...)` to make it look
// more "keyword-like" and draw attention to the fact it is a `noreturn` call.
//

#ifdef NDEBUG
    //
    // We don't want release builds to have to pay for the parameter
    // passing cost *or* the string table cost of having a list of all
    // the files and line numbers for all the places that originate
    // errors...
    //
    #define fail(error) \
        Fail_Core(error)
#else
    #ifdef CPLUSPLUS_11
        //
        // We can do a bit more checking in the C++ build, for instance to
        // make sure you don't pass a RELVAL* into fail().  This could also
        // be used by a strict build that wanted to get rid of all the hard
        // coded string fail()s, by triggering a compiler error on them.
        
        template <class T>
        inline static ATTRIBUTE_NO_RETURN void Fail_Core_Cpp(T *p) {
            static_assert(
                std::is_same<T, REBCTX>::value
                or std::is_same<T, const char>::value
                or std::is_same<T, const REBVAL>::value
                or std::is_same<T, REBVAL>::value,
                "fail() works on: REBCTX*, REBVAL*, const char*"
            );
            Fail_Core(p);
        }

        #define fail(error) \
            do { \
                Fail_Core_Cpp(error); \
            } while (0)
    #else
        #define fail(error) \
            do { \
                Fail_Core(error); \
            } while (0)
    #endif
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// PANIC (Force System Exit with Diagnostic Info)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Panics are the equivalent of the "blue screen of death" and should never
// happen in normal operation.  Generally, it is assumed nothing under the
// user's control could fix or work around the issue, hence the main goal is
// to provide the most diagnostic information possible.
//
// So the best thing to do is to pass in whatever REBVAL* or REBSER* subclass
// (including REBARR*, REBCTX*, REBACT*...) is the most useful "smoking gun":
//
//     if (VAL_TYPE(value) == REB_VOID)
//         panic (value);
//
//     if (ARR_LEN(array) < 2)
//         panic (array);
//
// Both the debug and release builds will spit out diagnostics of the item,
// along with the file and line number of the problem.  The diagnostics are
// written in such a way that they give the "more likely to succeed" output
// first, and then get more aggressive to the point of possibly crashing by
// dereferencing corrupt memory which triggered the panic.  The debug build
// diagnostics will be more exhaustive, but the release build gives some info.
//
// The most useful argument to panic is going to be a problematic value or
// series vs. a message (especially given that the file and line number are
// included in the report).  But if no relevant smoking gun is available, a
// UTF-8 string can also be passed to panic...and it will terminate with that
// as a message:
//
//     if (sizeof(foo) != 42) {
//         panic ("invalid foo size");
//
//         /* this line will never be reached, because it
//            immediately exited the process with a message */
//     }
//
// NOTE: It's desired that there be a space in `panic (...)` to make it look
// more "keyword-like" and draw attention to the fact it is a `noreturn` call.
//
#if defined(DEBUG_COUNT_TICKS)
    #ifdef NDEBUG
        #define panic(v) \
            Panic_Core((v), TG_Tick, NULL, 0)

        #define panic_at(v,file,line) \
            UNUSED(file); \
            UNUSED(line); \
            panic(v)
    #else
        #define panic(v) \
            Panic_Core((v), TG_Tick, __FILE__, __LINE__)

        #define panic_at(v,file,line) \
            Panic_Core((v), TG_Tick, (file), (line))
    #endif
#else
    #ifdef NDEBUG
        #define panic(v) \
            Panic_Core((v), 0, NULL, 0)

        #define panic_at(v,file,line) \
            UNUSED(file); \
            UNUSED(line); \
            panic(v)
    #else
        #define panic(v) \
            Panic_Core((v), 0, __FILE__, __LINE__)

        #define panic_at(v,file,line) \
            Panic_Core((v), 0, (file), (line))
    #endif
#endif
