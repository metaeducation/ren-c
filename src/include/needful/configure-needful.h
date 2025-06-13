//
//  file: %configure-needful.h
//  summary: "Configuration flags for the needful library"
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// It's best to use 0 and 1 to define things, to avoid typo problems when you
// just use `#ifdef SOME_MISPELT_FLAG` vs. `#if SOME_MISPELT_FLAG`.
//

#if !defined(RUNTIME_CHECKS)  // prefer "RUNTIME_CHECKS" as integer #define
    #if defined(NDEBUG)
       #define RUNTIME_CHECKS  0
    #else
       #define RUNTIME_CHECKS  1
    #endif
#endif
#if !defined(NO_RUNTIME_CHECKS)
    #define NO_RUNTIME_CHECKS (! RUNTIME_CHECKS)
#endif

#if !defined(DEBUG_STATIC_ANALYZING)
    #define DEBUG_STATIC_ANALYZING  0
#endif

#if !defined(CHECK_OPTIONAL_TYPEMACRO)
    #define CHECK_OPTIONAL_TYPEMACRO  0
#endif

#if !defined(CHECK_NEVERNULL_TYPEMACRO)
    #define CHECK_NEVERNULL_TYPEMACRO  0
#endif

#if !defined(DEBUG_USE_SINKS)
    #define DEBUG_USE_SINKS  0
#endif

#if !defined(DEBUG_CHECK_INIT_SINKS)
    #define DEBUG_CHECK_INIT_SINKS  0
#endif

#if !defined(ASSIGN_UNUSED_FIELDS)
    #define ASSIGN_UNUSED_FIELDS 1
#endif

#if !defined(PERFORM_CORRUPTIONS)  // 1. See Corrupt_If_Debug()
    #define PERFORM_CORRUPTIONS \
        (RUNTIME_CHECKS && (! DEBUG_STATIC_ANALYZING))  // [1]
#endif

#if PERFORM_CORRUPTIONS  // generate some variability, but still deterministic
  #if defined(__clang__)
    #define CORRUPT_IF_DEBUG_SEED 5  // e.g. fifth corrupt pointer is zero
    #define CORRUPT_IF_DEBUG_DOSE 11
  #else
    #define CORRUPT_IF_DEBUG_SEED 0  // e.g. first corrupt pointer is zero
    #define CORRUPT_IF_DEBUG_DOSE 7
  #endif
#endif
