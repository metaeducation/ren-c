//
//  File: %sys-trap.h
//  Summary: "CPU and Interpreter State Snapshot/Restore"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
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
// made with Make_Series() but never passed to either Free_Unmanaged_Series()
// or Manage_Series().  This covers several potential leaks known-to-Rebol,
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
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Mixing C++ and C code using longjmp is a recipe for disaster.  The plan
//   is that API primitives like rebRescue() will be able to abstract the
//   mechanism for fail, but for the moment only longjmp is implemented.
//

#if REBOL_FAIL_USES_LONGJMP
    #include <setjmp.h>
#endif


// R3-Alpha set up a separate `jmp_buf` at each point in the stack that wanted
// to be able to do a TRAP.  With stackless Ren-C, only one jmp_buf is needed
// per instance of the Trampoline on the stack.  (The codebase ideally does
// not invoke more than one trampoline to implement its native code, but if
// it is to call out to C code that wishes to use synchronous forms of API
// calls then nested trampolines may occur.)
//
// 1. We put the jmp_buf first, since it has alignment specifiers on Windows
//
// 2. When you run longjmp(), you can pass it a parameter which will be
//    the apparent result of the setjmp() when it gets teleported back to.
//    However, this parameter is only 32-bit even on 64-bit platforms, so it
//    is not enough to hold a pointer.  (C++'s `throw` can pass an arbitrary
//    value up to the `catch`, so this isn't needed in that case.)
//
// 3. Technically speaking, there's not a need for Fail_Core() to know what
//    the currently running frame is before it jumps...because any cleanup
//    it wants to do, it can do after the jump.  However, there's benefit
//    that if there's any "bad" situation noticed to be able to intercept
//    it before the stack state has been lost due to throw()/longjmp()...
//    a debugger has more information on hand.  For this reason, the
//    trampoline stores its concept of "current" frame in the jump structure
//    so it is available to Fail_Core() for automated or manual inspection.
//
struct Reb_Jump {
  #if REBOL_FAIL_USES_LONGJMP
    #ifdef HAS_POSIX_SIGNAL
        sigjmp_buf cpu_state;  // jmp_buf as first field of struct, see [1]
    #else
        jmp_buf cpu_state;
    #endif

    REBCTX *error;  // longjmp() case tunnels pointer back via this, see [2]
  #endif

    struct Reb_Jump *last_jump;

    Frame(*) frame;  // trampoline caches frame here for flexibility, see [3]
};


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
    // the some cross-platform builds on Linux targeting Windows were set
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


// TRAP_BLOCK is an abstraction of `try {} catch(...) {}` which can also
// work in plain C using setjmp/longjmp().  It's for trapping "abrupt errors",
// which are triggered the `fail` pseudo-"keyword" in C code.  These happen at
// arbitrary moments and are not willing (or able) to go through a normal
// `return` chain to pipe the error to the trampoline as a "thrown value".
//
// IN THE SETJMP IMPLEMENTATION...
//
// Jump buffers contain a pointer-to-a-REBCTX which represents an error.
// Using the tricky mechanisms of setjmp/longjmp, there will be a first pass
// of execution where setjmp() returns 0 and it will fall through to the
// code afterward.  When the longjmp() happens, the CPU will be teleported
// back to the setjmp(), where we have it return a 1...and goto the label
// for the `abrupt_failure`.
//
// On the line after the label, it takes the named variable you want to
// declare and assigns it the error pointer that had been assigned in the
// jump buffer.
//
// IN THE TRY/CATCH IMPLEMENTATION...
//
// With the setjmp() version of the macros, it's incidental that what follows
// the TRAP_BLOCK is a C scope {...}.  But it's critical to the TRY/CATCH
// version...because the last thing in the macro is a hanging `try` keyword.
// Similarly, what follows the ON_ABRUPT_FAILURE() need not be a scope in the
// setjmp() version, but must be a block for the hanging `catch(...)`.
//
// Because of that scope requirement, the only way to slip a named error
// variable into the subsequent scope is to actually make the error what the
// `throw` is passed.  Fortunately this works out exactly as one would want!
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. Although setjmp() does return a value, that value cannot be used in
//    conditions, e.g. `setjmp(...) ? x : y`
//
//    http://stackoverflow.com/questions/30416403/
//
// 2. setjmp() can't be used with inline functions in gcc:
//
//    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=24556
//
//    According to the developers, "This is not a bug as if you inline it,
//    the place setjmp goes to could be not where you want to goto."
//
// 3. DROP_TRAP_SAME_STACKLEVEL_AS_PUSH has a long name to remind you that
//    you must DROP_TRAP from the same scope you PUSH_TRAP from:
//
//      "If the function that called setjmp has exited (whether by return
//      or by a different longjmp higher up the stack), the behavior is
//      undefined. In other words, only long jumps up the call stack
//      are allowed." - http://en.cppreference.com/w/c/program/longjmp
//
#if REBOL_FAIL_USES_LONGJMP

    STATIC_ASSERT(REBOL_FAIL_USES_TRY_CATCH == 0);
    STATIC_ASSERT(REBOL_FAIL_JUST_ABORTS == 0);

    #define TRAP_BLOCK_IN_CASE_OF_ABRUPT_FAILURE \
        ; /* in case previous tatement was label */ \
        struct Reb_Jump jump;  /* one setjmp() per trampoline invocation */ \
        jump.last_jump = TG_Jump_List; \
        jump.frame = TOP_FRAME; \
        TG_Jump_List = &jump; \
        if (0 == SET_JUMP(jump.cpu_state))  /* beware return value, see [1] */ \
            jump.error = nullptr;  /* this branch will always be run */ \
        else \
            goto abrupt_failure; /* longjmp happened, jump.error will be set */

    #define DROP_TRAP_SAME_STACKLEVEL_AS_PUSH /* name is reminder, see [1] */ \
        assert(jump.error == nullptr); \
        TG_Jump_List = jump.last_jump;

    #define ON_ABRUPT_FAILURE(decl) \
        abrupt_failure: /* just a C label point */ \
            decl = jump.error; \
            jump.error = nullptr;

#elif REBOL_FAIL_USES_TRY_CATCH

    STATIC_ASSERT(REBOL_FAIL_JUST_ABORTS == 0);

    #define TRAP_BLOCK_IN_CASE_OF_ABRUPT_FAILURE \
        ; /* in case previous tatement was label */ \
        struct Reb_Jump jump; /* one per trampoline invocation */ \
        jump.last_jump = TG_Jump_List; \
        jump.frame = TOP_FRAME; \
        TG_Jump_List = &jump; \
        try /* picks up subsequent {...} block */

    #define DROP_TRAP_SAME_STACKLEVEL_AS_PUSH \
        TG_Jump_List = jump.last_jump;

    #define ON_ABRUPT_FAILURE(decl) \
        catch (decl) /* picks up subsequent {...} block */

#else

    STATIC_ASSERT(REBOL_FAIL_JUST_ABORTS);

    #define TRAP_BLOCK_IN_CASE_OF_ABRUPT_FAILURE \
        ; /* in case previous tatement was label */ \
        struct Reb_Jump jump; /* one per trampoline invocation */ \
        jump.last_jump = TG_Jump_List; \
        jump.frame = TOP_FRAME; \
        TG_Jump_List = &jump; \
        if (false) \
            goto abrupt_failure;  /* avoids unreachable code warning */

    #define DROP_TRAP_SAME_STACKLEVEL_AS_PUSH \
        TG_Jump_List = jump.last_jump;

    #define ON_ABRUPT_FAILURE(decl) \
      abrupt_failure: /* need to jump here to avoid unreachable warning */ \
        assert(!"ON_ABRUPT_FAILURE() reached w/REBOL_FAIL_JUST_ABORTS=1"); \
        decl = Error_User("REBOL_FAIL_JUST_ABORTS=1, should not reach!");

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
// 1. The C build wants a polymorphic fail() that can take error contexts,
//    UTF-8 strings, cell pointers...etc.  To do so requires accepting a
//    `const void*` which has no checking at compile-time.  The C++ build can
//    do better, and limit the input types that fail() will accept.
//
//   (This could be used by a strict build that wanted to get rid of all the
//    hard-coded string fail()s, by triggering a compiler error on them.)
//

#if DEBUG_PRINTF_FAIL_LOCATIONS
    #define Fail_Macro_Prelude(...) \
        printf(__VA_ARGS__)
#else
    #define Fail_Macro_Prelude(...) \
        NOOP
#endif

#if CPLUSPLUS_11  // add checking
    template <class T>
    inline static ATTRIBUTE_NO_RETURN void Fail_Macro_Helper(T *p) {
        static_assert(
            std::is_same<T, REBCTX>::value
            or std::is_same<T, const char>::value
            or std::is_base_of<const REBVAL, T>::value
            or std::is_base_of<Reb_Cell, T>::value,
            "fail() works on: REBCTX*, Cell(*), const char*"
        );
        Fail_Core(p);
    }
#else
    #define Fail_Macro_Helper Fail_Core
#endif

#if REBOL_FAIL_JUST_ABORTS
    #define fail panic
#else
    #define fail(error) do { \
        Fail_Macro_Prelude("fail() @ %s %d tick =", __FILE__, __LINE__); \
        Fail_Macro_Helper(error); /* prints the actual tick */ \
    } while (0)
#endif
