//
//  file: %needful.h
//  summary: "Tools for C with enhanced features if built as C++11 or higher"
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
// The "Needful" library is being factored out of the Ren-C codebase as an
// independent codebase.  It's still being worked out what will wind up in it.
//
// NOTE: Needful introduces creative TypeMacros like Option(T).  So we need
// special handling in %make-headers.r to recognize the format...
//
// ...see the `typemacro_parentheses` rule.
//
// Ren-C is designed to be able to build as C99 (or higher).
//
// BUT if the system is built as C++11 (or higher), there are extended runtime
// and compile-time checks available.
//
// This library contains various definitions for constructs that will behave
// in more "interesting" ways when built as C++.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Not all that many C99 features are required:
//
//      * __VA_ARGS__ variadic macros,
//      * double-slash comments
//      * declaring variables in the middle of functions
//
//   Many C89-era compilers could do these things before they were standards.
//   So there's a possibility Ren-C will compile on pre-C99 systems.
//
// * C++98 support was included for a while, but it lacks <type_traits> and
//   other features which are required to make the C++ build of any real use
//   beyond what C provides.  So support for C++98 was ultimately dropped.
//

#ifndef NEEDFUL_H  // "include guard" allows multiple #includes
#define NEEDFUL_H

#include "needful/configure-needful.h"

#include "needful/needful-utilities.h"

#include "needful/needful-casts.h"

#include "needful/needful-ensure.h"

#include "needful/needful-poison.h"

#include "needful/needful-corruption.h"

#include "needful/needful-option.h"

#include "needful/needful-sinks.h"

#include "needful/needful-loops.h"

#include "needful/needful-nevernull.h"

#include "needful/needful-extras.h"

#endif  // !defined(NEEDFUL_H)
