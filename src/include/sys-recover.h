//
//  file: %sys-recover.h
//  summary: "Abstraction of setjmp/longjmp and C++ throw/catch"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// This file implements a RECOVER_SCOPE abstraction of C++'s `try/catch`
// which can also be compiled as plain C using setjmp/longjmp().  It's for
// trapping "abrupt panics", that trigger from the `panic` pseudo-"keyword"
// in C code.  These happen at arbitrary moments and are not willing (or able)
// to go through a normal `return` chain to pipe an ERROR! up the stack.
//
// The abstraction is done with macros, and looks similar to try/catch...
// with a particularly clever trick in the C build using a for() loop to
// capture the Error variable in a scope!
//
//     int modified_local = 1020;
//     int unmodified_local = 304;
//     RECOVER_SCOPE_CLOBBERS_ABOVE_LOCALS_IF_MODIFIED {
//        //
//        // code that may trigger a panic() ...
//        //
//        modified_local = 0;  // modification means "clobbering"
//        CLEANUP_BEFORE_EXITING_RECOVER_SCOPE;  // necessary, unfortunately!
//     } ON_ABRUPT_PANIC (Error* e) {
//        //
//        // code that handles the error in `e`
//        //
//     }
//     // don't read modified_local here, it may have been clobbered
//     assert(unmodified_local == 304);  // this is safe, not clobbered
//
// Being able to build either way has important benefits, beyond just the
// stubborn insistence that Rebol can compile as C99.  Some older/simpler
// platforms only offer setjmp/longjmp and do not have exceptions, while some
// newer structured platforms (like WebAssembly) may only offer exceptions (or
// if they do offer setjmp/longjmp, the emulation is brittle and slow.)
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * In Rebol terminology, abrupt panics triggered by panic() are mechanically
//   distinct from cooperative panics, e.g. `panic ()`...which does
//   *not* use exceptions or longjmp().  Instead a native implementation
//   must go all the way to the `return` statement to say `panic ()`.
//
// * To help Rebol clean up after itself for some kinds of "dangling" state,
//   it will automatically free manually memory managed Flexes made with
//   Make_Flex() but never passed to either Free_Unmanaged_Flex() or
//   Manage_Stub().  These Flexes are used to implement rebAlloc() so
//   that allocations will be automatically freed on failure.  But if you've
//   written code that performs a raw malloc and triggers an abrupt failure
//   up the stack, it will leak the malloc.
//
// * Mixing C++ and C code using longjmp is a recipe for disaster.  Currently
//   the uses of C++ features are limited, and only in checked builds.  So it
//   shouldn't be too much of a problem to build with the C++/debug/longjmp
//   combination...but there may be some issues.
//

#if PANIC_USES_LONGJMP
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
struct JumpStruct {
  #if PANIC_USES_LONGJMP
    #if HAS_POSIX_SIGNAL
        sigjmp_buf cpu_state;  // jmp_buf as first field of struct [1]
    #else
        jmp_buf cpu_state;
    #endif

    Error* error;  // longjmp() case tunnels pointer back via this [2]
  #endif

    struct JumpStruct* last_jump;

  #if PANIC_USES_LONGJMP
    bool clean_exit;  // debug flag, but #ifdef'ing macro code would be a pain
  #endif

  #if PANIC_USES_TRY_CATCH
    ~JumpStruct() {  // we don't get control otherwise when exceptions happen
        g_ts.jump_list = this->last_jump;
    }
  #elif PANIC_USES_LONGJMP && CPLUSPLUS_11 && RUNTIME_CHECKS
    JumpStruct() {
        clean_exit = false;
    }
    ~JumpStruct() {
        if (not clean_exit)
            assert("Missing CLEANUP_BEFORE_EXITING_RECOVER_SCOPE() call");
    }
  #endif
};


////// RECOVER_SCOPE ABSTRACTION /////////////////////////////////////////////
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
// for the `on_abrupt_panic`.
//
// On the line after the label, it takes the named variable you want to
// declare and assigns it the error pointer that had been assigned in the
// jump buffer.
//
// IN THE TRY/CATCH IMPLEMENTATION...
//
// With the setjmp() version of the macros, it's incidental that what follows
// the RECOVER_SCOPE is a C scope {...}.  But it's critical to the TRY/CATCH
// version...because the last thing in the macro is a hanging `try` keyword.
// Similarly, what follows the ON_ABRUPT_PANIC() need not be a scope in the
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
//    to manually call a CLEANUP_BEFORE_EXITING_RECOVER_SCOPE macro if you
//    are going to exit the rescued scope prematurely.
//
//    By having the non-longjmp() versions do the same work, we help ensure
//    that the longjmp() build stays working by detecting imbalances.
//
//    TECHNICALLY IT WOULDN'T BE NEEDED IF YOU FALL THROUGH NORMALLY.  But
//    by having it required in all cases, this helps cue any premature
//    exits at source level to do the work.
//
// 6. The way that the longjmp() version works, it does a goto out of the
//    scope being rescued, to the same place that would be reached from a
//    fallthrough with no abrupt panic.  Unfortunately, the decision to
//    run the code block in the ON_ABRUPT_PANIC() macro is made based on
//    testing if the jump error state is null, and the compiler doesn't know
//    you can never fall through with a non-null error state if the block
//    being rescued doesn't return.

#if PANIC_USES_LONGJMP

    STATIC_ASSERT(PANIC_USES_TRY_CATCH == 0);
    STATIC_ASSERT(PANIC_JUST_ABORTS == 0);

    #if defined(__MINGW64__) && (__GNUC__ < 5)  // [1]
        #define SET_JUMP(s)     __builtin_setjmp(s)
        #define LONG_JUMP(s,v)  __builtin_longjmp((s), (v))
    #elif HAS_POSIX_SIGNAL  // [2]
        #define SET_JUMP(s)     sigsetjmp((s), 1)
        #define LONG_JUMP(s,v)  siglongjmp((s), (v))
    #else
        #define SET_JUMP(s)     setjmp(s)
        #define LONG_JUMP(s,v)  longjmp((s), (v))
    #endif

    #define RECOVER_SCOPE_CLOBBERS_ABOVE_LOCALS_IF_MODIFIED \
        NOOP;  /* stops warning when previous statement was label */ \
        Jump jump;  /* one setjmp() per trampoline invocation */ \
        jump.last_jump = g_ts.jump_list; \
        jump.error = nullptr; \
        g_ts.jump_list = &jump; \
        if (1 == SET_JUMP(jump.cpu_state))  /* beware return value [3] */ \
            goto on_longjmp_or_scope_exited; /* jump.error will be set */ \
        /* fall through to subsequent block, happens on first SET_JUMP() */

    #define CLEANUP_BEFORE_EXITING_RECOVER_SCOPE /* can't avoid [5] */ \
        assert(jump.error == nullptr); \
        g_ts.jump_list = jump.last_jump; \
        jump.clean_exit = true

    INLINE Error* Test_And_Clear_Jump_Error(Jump* jump)
    {
        if (not jump->error)
            return nullptr;
        Error* error = jump->error;
        jump->error = nullptr;
        g_ts.jump_list = jump->last_jump;
        jump->clean_exit = true;
        return error;
    }

    #define ON_ABRUPT_PANIC(decl) \
      on_longjmp_or_scope_exited: \
        if (jump.error) \
            for (decl = jump.error; Test_And_Clear_Jump_Error(&jump); ) \
                /* fall through to subsequent block */
        /* must have code for fallthrough after the abrupt panic block [6] */

#elif PANIC_USES_TRY_CATCH

    STATIC_ASSERT(PANIC_JUST_ABORTS == 0);

    #define RECOVER_SCOPE_CLOBBERS_ABOVE_LOCALS_IF_MODIFIED \
        NOOP;  /* stops warning when previous statement was label */ \
        Jump jump; /* one per trampoline invocation */ \
        jump.last_jump = g_ts.jump_list; \
        g_ts.jump_list = &jump; \
        try /* picks up subsequent {...} block */

    #define CLEANUP_BEFORE_EXITING_RECOVER_SCOPE /* can't avoid [5] */ \
        /* assert(jump.error == nullptr); */ /* not needed w/C++ catch */ \
        /* g_ts.jump_list = jump.last_jump; */ /* destructor does it */ \
        /* jump.clean_exit = true */  /* C++ leaves loop without jump */ \
        NOOP

    #define ON_ABRUPT_PANIC(decl) \
        catch (decl) /* picks up subsequent {...} block */
        /* must have code for fallthrough after the abrupt panic block [6] */

#else

    STATIC_ASSERT(PANIC_JUST_ABORTS);

    #define RECOVER_SCOPE_CLOBBERS_ABOVE_LOCALS_IF_MODIFIED \
        NOOP;  /* stops warning when previous statement was label */ \
        Jump jump; /* one per trampoline invocation */ \
        jump.last_jump = g_ts.jump_list; \
        g_ts.jump_list = &jump; \
        if (false) \
            goto on_abrupt_panic;  /* avoids unreachable code warning */

    #define CLEANUP_BEFORE_EXITING_RECOVER_SCOPE /* can't avoid [5] */ \
        /* assert(jump.error == nullptr); */ /* no error in this version */ \
        g_ts.jump_list = jump.last_jump; \
        /* jump.clean_exit = true */ /* not applicable */ \
        NOOP

    #define ON_ABRUPT_PANIC(decl) \
        NOOP; \
        decl; \
      on_abrupt_panic: /* impossible jump here to avoid unreachable warning */ \
        assert(!"ON_ABRUPT_PANIC() reached w/PANIC_JUST_ABORTS=1"); \
        /* must have code for fallthrough after the abrupt panic block [6] */
#endif


//=//// *NON-COOPERATIVE* ABRUPT panic() MECHANISM /////////////////////////=//
//
// "Abrupt Failures" come in "cooperative" and "uncooperative" forms.  The
// cooperative form happens when a native's C code does `panic (...)`,
// and should be used when possible, as it is more efficient and also will
// work on platforms that don't have exception handling or longjmp().
//
// But the uncooperative form of `panic (...)` can be called at any moment,
// and is what the RECOVER_SCOPE() abstraction is designed to catch:
//
//     if (Foo_Type(foo) == BAD_FOO) {
//         panic (Error_Bad_Foo_Operation(...));
//
//         /* this line will never be reached, because it longjmp'd or
//            C++ throw'd up the stack where execution continues */
//     }
//
// Errors that originate from C code are created via Make_Error, and are
// defined in %errors.r.  These definitions contain a formatted message
// template, showing how the arguments will be displayed in FORMing.
//
// NOTE: It's desired that there be a space in `panic (...)` to make it look
// more "keyword-like" and draw attention to the fact it is a `noreturn` call.
//
// 1. The C build wants a polymorphic panic() that can take error contexts,
//    UTF-8 strings, cell pointers...etc.  To do so requires accepting a
//    `const void*` which has no checking at compile-time.  The C++ build can
//    do better, and limit the input types that panic() will accept.
//
//   (This could be used by a strict build that wanted to get rid of all the
//    hard-coded string panic()s, by triggering a compiler error on them.)
//

#if DEBUG_PRINTF_PANIC_LOCATIONS
    #define Panic_Prelude_File_Line_Tick(...) \
        printf("panic() FILE %s LINE %d TICK %" PRIu64 "\n", __VA_ARGS__)
#else
    #define Panic_Prelude_File_Line_Tick(...) \
        NOOP
#endif


#if CPLUSPLUS_11  // add type checking of panic() argument in C++ build [1]
    template <class T>
    INLINE Error* Derive_Error_From_Pointer(T* p) {
        static_assert(
            std::is_same<T, Error>::value
            or std::is_same<T, const char>::value
            or std::is_base_of<const Value, T>::value
            or std::is_base_of<Cell, T>::value,
            "Derive_Error_From_Pointer() works on: Error*, Cell*, const char*"
        );
        return Derive_Error_From_Pointer_Core(p);
    }

  #if NEEDFUL_SINK_USES_WRAPPER
    template <class T>
    INLINE Error* Derive_Error_From_Pointer(Sink(T) sink)
      { return Derive_Error_From_Pointer(sink.p); }

    template <class T>
    INLINE Error* Derive_Error_From_Pointer(Need(T*) need)
      { return Derive_Error_From_Pointer(need.p); }
  #endif
#else
    #define Derive_Error_From_Pointer  Derive_Error_From_Pointer_Core
#endif


#if PANIC_JUST_ABORTS

    #define Needful_Panic_Abruptly(p) do { \
        Panic_Prelude_File_Line_Tick(__FILE__, __LINE__, TICK), \
        crash (Panic_Abruptly_Helper(Derive_Error_From_Pointer(p))); \
    } while (0)

#elif PANIC_USES_TRY_CATCH

    #define Needful_Panic_Abruptly(p) do { \
        Panic_Prelude_File_Line_Tick(__FILE__, __LINE__, TICK), \
        throw Panic_Abruptly_Helper(Derive_Error_From_Pointer(p)); \
    } while (0)

#else
    STATIC_ASSERT(PANIC_USES_LONGJMP);

    // "If the function that called setjmp has exited (whether by return or
    //  by a different longjmp higher up the stack), the behavior is undefined.
    //  In other words, only long jumps up the call stack are allowed."
    //
    //  http://en.cppreference.com/w/c/program/longjmp

    #define Needful_Panic_Abruptly(p) do { \
        Panic_Prelude_File_Line_Tick(__FILE__, __LINE__, TICK), \
        g_ts.jump_list->error = Panic_Abruptly_Helper( \
            Derive_Error_From_Pointer(p)  /* longjmp() arg too small */ \
        ); \
        LONG_JUMP(g_ts.jump_list->cpu_state, 1);  /* 1 is setjmp() return */ \
    } while (0)

#endif


//=//// NEEDFUL HOOKS FOR ERROR HANDLING //////////////////////////////////=//
//
// The needful-result.h file defines macros that are used to handle errors
// based on global error state.  But it doesn't hardcode how that state is
// set or cleared, you have to define them.
//

INLINE Error* Needful_Test_And_Clear_Failure(void) {
    Error* temp = g_failure;  // Option(Error*) optimized [1]
    g_failure = nullptr;
    return temp;
}

#if NO_RUNTIME_CHECKS
    #define Needful_Set_Failure(p) \
        (g_failure = Derive_Error_From_Pointer(p))
#else
    INLINE void Inline_Needful_Set_Failure(Error* error) {
        g_failure = error;  // to set breakpoints here
    }
    #define Needful_Set_Failure(p) \
        Inline_Needful_Set_Failure(Derive_Error_From_Pointer(p))
#endif

#define Needful_Get_Failure() \
    g_failure

#define Needful_Assert_Not_Failing() \
    assert(not g_failure)
