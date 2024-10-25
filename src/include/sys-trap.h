//
//  File: %sys-trap.h
//  Summary: "CPU and Interpreter State Snapshot/Restore"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// This file implements a RESCUE_SCOPE abstraction of C++'s `try/catch`
// which can also be compiled as plain C using setjmp/longjmp().  It's for
// trapping "abrupt errors", that trigger from the `fail` pseudo-"keyword"
// in C code.  These happen at arbitrary moments and are not willing (or able)
// to go through a normal `return` chain to pipe a raised ERROR! up the stack.
//
// The abstraction is done with macros, and looks similar to try/catch:
//
//     RESCUE_SCOPE_IN_CASE_OF_ABRUPT_FAILURE {
//        //
//        // code that may trigger a fail() ...
//        //
//     } ON_ABRUPT_FAILURE(VarList* e) {
//        //
//        // code that handles the error in `e`
//        //
//     }
//
// Being able to build either way has important benefits, beyond just the
// stubborn insistence that Rebol can compile as C99.  Some older/simpler
// platforms only offer setjmp/longjmp and do not have exceptions, while some
// newer structured platforms (like WebAssembly) may only offer exceptions (or
// if they do offer setjmp/longjmp, the emulation is brittle and slow.)
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * In Rebol terminology, abrupt errors triggered by "fail" are mechanically
//   distinct from a "throw".  Rebol THROW is a cooperative concept, which
//   does *not* use exceptions or longjmp().  Instead a native implementation
//   must go all the way to the `return` statement to say `return THROWN;`.
//
// * To help Rebol clean up after itself for some kinds of "dangling" state,
//   it will automatically free manually memory managed Flexes made with
//   Make_Flex() but never passed to either Free_Unmanaged_Flex() or
//   Manage_Flex().  These Flexes are used to implement rebAlloc() so
//   that allocations will be automatically freed on failure.  But if you've
//   written code that performs a raw malloc and triggers an abrupt failure
//   up the stack, it will leak the malloc.
//
// * Mixing C++ and C code using longjmp is a recipe for disaster.  Currently
//   the uses of C++ features are limited, and only in debug builds.  So it
//   shouldn't be too much of a problem to build with the C++/debug/longjmp
//   combination...but there may be some issues.
//

#if REBOL_FAIL_USES_LONGJMP
    #include <setjmp.h>
#endif


// R3-Alpha set up a separate `jmp_buf` at each point in the stack that wanted
// to be able catch failures.  With stackless Ren-C, only one jmp_buf is needed
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
//    the currently running level is before it jumps...because any cleanup
//    it wants to do, it can do after the jump.  However, there's benefit
//    that if there's any "bad" situation noticed to be able to intercept
//    it before the stack state has been lost due to throw()/longjmp()...
//    a debugger has more information on hand.  For this reason, the
//    trampoline stores its concept of "current" level in the jump structure
//    so it is available to Fail_Core() for automated or manual inspection.
//
struct JumpStruct {
  #if REBOL_FAIL_USES_LONGJMP
    #ifdef HAS_POSIX_SIGNAL
        sigjmp_buf cpu_state;  // jmp_buf as first field of struct [1]
    #else
        jmp_buf cpu_state;
    #endif

    Error* error;  // longjmp() case tunnels pointer back via this [2]
  #endif

    struct JumpStruct* last_jump;

    Level* level;  // trampoline caches level here for flexibility [3]
};


////// RESCUE_SCOPE ABSTRACTION //////////////////////////////////////////////
//
// This is a pretty clever bit of design trickery, if I do say so myself!
//
// IN THE SETJMP IMPLEMENTATION...
//
// (See: https://en.wikipedia.org/wiki/Setjmp.h#Exception_handling)
//
// Jump buffers contain a pointer-to-a-VarList* which represents an error.
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
// the RESCUE_SCOPE is a C scope {...}.  But it's critical to the TRY/CATCH
// version...because the last thing in the macro is a hanging `try` keyword.
// Similarly, what follows the ON_ABRUPT_FAILURE() need not be a scope in the
// setjmp() version, but must be a block for the hanging `catch(...)`.
//
// Because of that scope requirement, the only way to slip a named error
// variable into the subsequent scope is to actually make the error what the
// `throw` is passed.  Fortunately this works out exactly as one would want!
//
// C++ exceptions have an added bonus, that most compilers can provide a
// benefit of avoiding paying for catch blocks unless an exception occurs.
// This is called "zero-cost exceptions":
//
//   https://stackoverflow.com/q/15464891/ (description of the phenomenon)
//   https://stackoverflow.com/q/38878999/ (note on needing linker support)
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. 64-bit builds made by MinGW in the 4.x range have an unfortunate bug in
//    the setjmp/longjmp mechanic, which causes hangs for reasons that are
//    seemingly random, like "using -O0 optimizations instead of -O2".
//    This issue is unfortunately common in some older cross-compilers:
//
//    https://sourceforge.net/p/mingw-w64/bugs/406/
//
// 2. "Under FreeBSD 5.2.1 and Mac OS X 10.3, setjmp and longjmp save and
//     restore the signal mask. Linux 2.4.22 and Solaris 9, however, do not
//     do this.  FreeBSD and Mac OS X provide the functions _setjmp and
//     _longjmp, which do not save and restore the signal mask."
//
//    "To allow either form of behavior, POSIX.1 does not specify the effect
//     of setjmp and longjmp on signal masks.  Instead, two new functions,
//     sigsetjmp and siglongjmp, are defined by POSIX.1.  These two functions
//     should always be used when branching from a signal handler."
//
// 3. Although setjmp() does return a value, that value cannot be used in
//    conditions, e.g. `setjmp(...) ? x : y`
//
//    http://stackoverflow.com/questions/30416403/
//
// 4. setjmp() can't be used with inline functions in gcc.  According to the
//    developers, "This is not a bug as if you inline it, the place setjmp
//    goes to could be not where you want to goto."
//
//    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=24556
//
// 5. Sadly, there's no real way to make the C version "automagically" know
//    when you've done a `return` out of the trapped block.  As a result, the
//    notion of which CPU buffer to jump to cannot be updated--which is a
//    requirement when nested instances of RESCUE are allowed.  So you have
//    to manually call a CLEANUP_BEFORE_EXITING_RESCUE_SCOPE macro.
//
//    We make the best of it by using it as an opportunity to keep other
//    information up to date, like letting the system globally know what
//    level the trampoline currently is running (which may not be TOP_LEVEL).
//
#if REBOL_FAIL_USES_LONGJMP

    STATIC_ASSERT(REBOL_FAIL_USES_TRY_CATCH == 0);
    STATIC_ASSERT(REBOL_FAIL_JUST_ABORTS == 0);

    #if defined(__MINGW64__) && (__GNUC__ < 5)  // [1]
        #define SET_JUMP(s)     __builtin_setjmp(s)
        #define LONG_JUMP(s,v)  __builtin_longjmp((s), (v))
    #elif defined(HAS_POSIX_SIGNAL)  // [2]
        #define SET_JUMP(s)     sigsetjmp((s), 1)
        #define LONG_JUMP(s,v)  siglongjmp((s), (v))
    #else
        #define SET_JUMP(s)     setjmp(s)
        #define LONG_JUMP(s,v)  longjmp((s), (v))
    #endif

    #define RESCUE_SCOPE_IN_CASE_OF_ABRUPT_FAILURE \
        NOOP; /* stops warning when case previous statement was label */ \
        Jump jump;  /* one setjmp() per trampoline invocation */ \
        jump.last_jump = g_ts.jump_list; \
        jump.level = TOP_LEVEL; \
        jump.error = nullptr; \
        g_ts.jump_list = &jump; \
        if (1 == SET_JUMP(jump.cpu_state))  /* beware return value [3] */ \
            goto longjmp_happened; /* jump.error will be set */ \
        /* fall through to subsequent block, happens on first SET_JUMP() */

    #define CLEANUP_BEFORE_EXITING_RESCUE_SCOPE /* can't avoid [5] */ \
        assert(jump.error == nullptr); \
        g_ts.jump_list = jump.last_jump

    #define ON_ABRUPT_FAILURE(decl) \
      longjmp_happened: \
        NOOP; /* must be statement after label */ \
        decl = jump.error; \
        jump.error = nullptr; \
        /* fall through to subsequent block */

#elif REBOL_FAIL_USES_TRY_CATCH

    STATIC_ASSERT(REBOL_FAIL_JUST_ABORTS == 0);

    #define RESCUE_SCOPE_IN_CASE_OF_ABRUPT_FAILURE \
        ; /* in case previous tatement was label */ \
        Jump jump; /* one per trampoline invocation */ \
        jump.last_jump = g_ts.jump_list; \
        jump.level = TOP_LEVEL; \
        g_ts.jump_list = &jump; \
        try /* picks up subsequent {...} block */

    #define CLEANUP_BEFORE_EXITING_RESCUE_SCOPE /* can't avoid [5] */ \
        g_ts.jump_list = jump.last_jump

    #define ON_ABRUPT_FAILURE(decl) \
        catch (decl) /* picks up subsequent {...} block */

#else

    STATIC_ASSERT(REBOL_FAIL_JUST_ABORTS);

    #define RESCUE_SCOPE_IN_CASE_OF_ABRUPT_FAILURE \
        ; /* in case previous tatement was label */ \
        Jump jump; /* one per trampoline invocation */ \
        jump.last_jump = g_ts.jump_list; \
        jump.level = TOP_LEVEL; \
        g_ts.jump_list = &jump; \
        if (false) \
            goto abrupt_failure;  /* avoids unreachable code warning */

    #define CLEANUP_BEFORE_EXITING_RESCUE_SCOPE /* can't avoid [5] */ \
        g_ts.jump_list = jump.last_jump

    #define ON_ABRUPT_FAILURE(decl) \
      abrupt_failure: /* impossible jump here to avoid unreachable warning */ \
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
    #define Fail_Prelude_File_Line_Tick(...) \
        printf("fail() FILE %s LINE %d TICK %" PRIu64 "\n", __VA_ARGS__)
#else
    #define Fail_Prelude_File_Line_Tick(...) \
        NOOP
#endif

#if CPLUSPLUS_11  // add checking
    template <class T>
    INLINE ATTRIBUTE_NO_RETURN void Fail_Macro_Helper(T *p) {
        static_assert(
            std::is_same<T, Error>::value
            or std::is_same<T, const char>::value
            or std::is_base_of<const Value, T>::value
            or std::is_base_of<Cell, T>::value,
            "fail() works on: Error*, Cell*, const char*"
        );
        Fail_Core(p);
    }

  #if DEBUG_USE_CELL_SUBCLASSES
    template <class T>
    INLINE ATTRIBUTE_NO_RETURN void Fail_Macro_Helper(Sink(T) sink)
      { Fail_Core(sink.p); }

    template <class T>
    INLINE ATTRIBUTE_NO_RETURN void Fail_Macro_Helper(Need(T) need)
      { Fail_Core(need.p); }
  #endif
#else
    #define Fail_Macro_Helper Fail_Core
#endif

#if REBOL_FAIL_JUST_ABORTS
    #define fail panic
#else
    #define fail(error) do { \
        Fail_Prelude_File_Line_Tick(__FILE__, __LINE__, TICK), \
        Fail_Macro_Helper(error); /* prints the actual tick */ \
    } while (0)
#endif
