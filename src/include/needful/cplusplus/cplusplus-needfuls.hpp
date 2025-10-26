//
//  file: %cplusplus-needfuls.hpp
//  summary: "Performs #includes of macro overrides of %needful.h with C++"
//  homepage: <needful homepage TBD>
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=////////////////////////////////////////////////////////////////////////=//
//
// While %needful.h is specifically stylized to be a single-file header, the
// C++ implementation of the wrapper classes in Needful is complicated enough
// that it is split into multiple files--as many as it seems necessary.
//
// But remember: if you're shipping your source code to someone to build,
// all they need is %needful.h - so you could go ahead and commit it to your
// source tree.  Then the only people who have to worry about getting a copy
// of the C++ implementation are those who want to do active development on
// it and get the benefits of the advanced compile-time checks.
//
// WARNING: Once you have the checks, you won't want to develop without them!
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. All the required #include statements for the advanced features are kept
//    here in %cplusplus-needfuls.h for transparency, so you can see what all
//    gets included.
//


#if !defined(NEEDFUL_H_INCLUDED)
    #error "You must include needful.h before cplusplus-needfuls.hpp"
#endif

#if !defined(__cplusplus)
    #error "You must include cplusplus-needfuls.hpp only in C++ builds"
#endif


//=//// <utility> FOR std::forward() //////////////////////////////////////=//
//
// <type_traits> is already included by needful.h to implement the nocast
// facility, required for C++ builds (even with NEEDFUL_NO_ENHANCEMENTS).
// For the uninitiated: type traits enables most of the magic that Needful
// does...it's a library of queries you can perform at compile-time, and guide
// the behavior of things from the answers:
//
//      https://en.cppreference.com/w/cpp/header/type_traits.html
//
// It's a powerful static-analysis tool that comes built into the compiler
// you already have!
//
// However we also need <utility> here for std::forward().
//

#include <utility>


//=//// <stdint.h> FOR MAKING POINTERS FROM INTEGERS //////////////////////=//
//
// There's no portable way to cast an integer to a pointer in C++ without
// using uintptr_t or intptr_t.  %needful-corruption.h uses pointer assigns
// from integers to do fast corruption.
//

#include <stdint.h>


//=//// <iso646.h> for `and`, `not` etc. //////////////////////////////////=//
//
// In an attempt to make Needful as agnostic as possible in terms of its
// C interface, %needful.h uses ! and && and || instead of trying to make
// use of the C++ keywords `and`, `not`, etc.  But writing and maintaining
// the Needful overrides is a lot nicer using the keywords, so we use them.
//
// But MSVC doesn't enable them by default.  This is pretty crazy, because
// C++ compilers *should* define them, they're even in the C++98 standard!
//
// Note that while these "alternative tokens" are part of C++, they are not
// required to be recognized by the preprocessor.
//
//     #if not 0  // <-- This is NOT portable or standard!
//        ...
//
// So preprocessor directives have to use the symbols.
//

#if defined(_MSC_VER)
  #include <iso646.h>  // MSVC doesn't have `and`, `not`, etc. w/o this
#endif


//=//// assert() MUST BE DEFINED BEFORE USING EXTENDED NEEDFUL ////////////=//
//
// The C++ implementation uses assert(), but doesn't #include <assert.h> on
// its own, as there may be overriding assert() definitions desired.
//
// (Some assert implementations provided by the system are antagonistic to
// debuggers and need replacement.)
//

#if !defined(assert)  // must define assert() [2]
    #error "Include <assert.h> or assert-fix.h before including needful.h"
    #include <stophere>  // https://stackoverflow.com/a/45661130
#endif


//=//// NEEDFUL_DONT_INCLUDE_STDARG_H /////////////////////////////////////=//
//
// Not all clients necessarily want to #include <stdarg.h> ... it may not
// be available on the platform or could cause problems if included in some
// codebases.  Default to including it since it offers protections people may
// not be aware are necessary, but allow it to be turned off.
//

#if !defined(NEEDFUL_DONT_INCLUDE_STDARG_H)
    #define NEEDFUL_DONT_INCLUDE_STDARG_H  0
#else
    STATIC_ASSERT(NEEDFUL_DONT_INCLUDE_STDARG_H == 1 or
                NEEDFUL_DONT_INCLUDE_STDARG_H == 0);
#endif

#if (! NEEDFUL_DONT_INCLUDE_STDARG_H)  // may not want to include it... [3]
    #include <stdarg.h>  // ...but helps cast() catch bad va_list usages
#endif


//=//// INCLUDE THE NEEDFUL OVERRIDES /////////////////////////////////////=//
//
// 1. It's technically most correct to scope things with `::needful::`, which
//    would allow you to have local variables called needful that would not
//    conflict with the namespace.  This is a little bit uglier for not
//    a lot of benefit, so until someone can think of a good reason to do it
//    we just scope with `needful::`

namespace needful {  //=//// BEGIN `needful::` NAMESPACE //////////////////=//

    #include "needful-utilities.hpp"

    #include "needful-asserts.hpp"

    #include "needful-ensure.hpp"

    #include "needful-wrapping.hpp"

    #include "needful-const.hpp"

    #include "needful-casts.hpp"

  #if NEEDFUL_DOES_CORRUPTIONS
    #include "needful-corruption.hpp"
  #endif

  #if NEEDFUL_OPTION_USES_WRAPPER
    #include "needful-option.hpp"
  #endif

  #if NEEDFUL_RESULT_USES_WRAPPER
    #include "needful-result.hpp"
  #endif

  #if NEEDFUL_SINK_USES_WRAPPER
    #include "needful-sinks.hpp"
  #endif

} //=//// END `needful::` NAMESPACE ///////////////////////////////////////=//
