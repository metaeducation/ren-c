//
//  File: %assert-fix.h
//  Summary: "An alternative to what you get from #include <assert.h>"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// For some reason, Windows implementation of "_wassert" corrupts the stack
// by calling abort(), to where you only see at most 3 C stack frames above
// the assert in the VSCode debugger.  That's unusable.
//
// Also, in the Chromium debugger for WebAssembly an assert() causes a
// termination with no ability to inspect the stack.  It's nice to be able
// have a place to set a breakpoint, as well as to potentially continue.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * assert() is enabled by default; disable with `#define NDEBUG`
//
//   http://stackoverflow.com/a/17241278
//

#if !defined(NDEBUG) && !defined(USE_STANDARD_ASSERT_H)

    #include <assert.h>  // include so it will think it has been included
    #undef assert  // (this way its include guard prevents it defining again)

    #include "debugbreak.h"
    #include <stdio.h>

    inline static void Assertion_Failure(
        const char* file,
        int line,
        const char* expr
    ){
        printf("Assertion failure: %s\n", expr);
        printf("Line %d, File: %s\n", line, file);
        debug_break();  // calling debug_break() allows us to step afterward
    }

    #define assert(expr) \
        ((expr) ? (void)0 : Assertion_Failure(__FILE__, __LINE__, #expr))
#else
    #include <assert.h>

  // There is a bug in older GCC where the assert macro expands arguments
  // unnecessarily.  Since Rebol tries to build on fairly old systems, this
  // patch corrects the issue:
  //
  // https://sourceware.org/bugzilla/show_bug.cgi?id=18604
  //
  #if !defined(NDEBUG) && defined(__GLIBC__) && defined(__GLIBC_MINOR__) \
      && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 23))

      #undef assert
    #if !defined(__GNUC__) || defined (__STRICT_ANSI__)
      #define assert(expr) \
          ((expr) \
              ? __ASSERT_VOID_CAST (0) \
              : __assert_fail (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION))
    #else
      #define assert(expr) \
          ({ \
            if (expr) \
              ; /* empty */ \
            else \
              __assert_fail (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION); \
          })
      #endif
  #endif
#endif
