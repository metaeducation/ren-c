//
//  File: %reb-config.h
//  Summary: "General build configuration"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
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
// This is the first file included by %sys-core.h.
//
// Many of the flags controlling the build (such as
// the TO_<target> definitions) come from -DTO_<target> in the
// compiler command-line.  These command lines are generally
// produced automatically, based on the build that is picked
// from %systems.r.
//
// However, some flags require the preprocessor's help to
// decide if they are relevant, for instance if they involve
// detecting features of the compiler while it's running.
// Or they may adjust a feature so narrowly that putting it
// into the system configuration would seem unnecessary.
//
// Over time, this file should be balanced and adjusted with
// %systems.r in order to make the most convenient and clear
// build process.  If there is difficulty in making a build
// work on a system, use that as an opportunity to reflect
// how to make this better.
//


/** Primary Configuration **********************************************

The primary target system is defined by:

    TO_(os-base)    - for example TO_WINDOWS or TO_LINUX
    TO_(os-name)    - for example TO_WINDOWS_X86 or TO_LINUX_X64

The default config builds an R3 HOST executable program.

To change the config, host-kit developers can define:

    REB_EXT         - build an extension module
                      * create a DLL, not a host executable
                      * do not export a host lib (OS_ lib)
                      * call r3lib via struct and macros

    REB_CORE        - build /core only, no graphics, windows, etc.

Special internal defines used by RT, not Host-Kit developers:

    REB_API         - build r3lib as API
                      * export r3lib functions
                      * build r3lib dispatch table
                      * call host lib (OS_) via struct and macros

    REB_EXE         - build r3 as a standalone executable
*/

//* Common *************************************************************


#ifdef REB_EXE
    // standalone exe from RT
    // Export all of the APIs such that they can be referenced by extensions.
    // The purpose is to have one exe and some dynamic libraries for extensions (.dll, .so etc.)
    #define RL_API API_EXPORT
#else
    #ifdef REB_API
        // r3lib dll from RT
        #define RL_API API_EXPORT
    #elif defined(EXT_DLL) || defined(REB_HOST)
        // Building extensions as external libraries (.dll, .so etc.)
        // or r3 host against r3lib dll
        #define RL_API API_IMPORT
    #else
        // Extensions are builtin
        #define RL_API
    #endif
#endif



//* MS Windows ********************************************************

#ifdef TO_WINDOWS_X86
#endif

#ifdef TO_WINDOWS_X64
#endif

#ifdef TO_WINDOWS
    // ASCII strings to Integer
    #define ATOI                    // supports it
    #define ATOI64                  // supports it
    #define ITOA64                  // supports it

    // Used when we build REBOL as a DLL:
    #define API_EXPORT __declspec(dllexport)
    #define API_IMPORT __declspec(dllimport)

#else
    #define API_IMPORT
    // Note: Unsupported by gcc 2.95.3-haiku-121101
    // (We #undef it in the Haiku section)
    #define API_EXPORT __attribute__((visibility("default")))
#endif


//* Linux ********************************************************

#ifdef TO_LINUX_X86
#endif

#ifdef TO_LINUX_X64
#endif

#ifdef TO_LINUX_PPC
#endif

#ifdef TO_LINUX_ARM
#endif

#ifdef TO_LINUX_AARCH64
#endif

#ifdef TO_LINUX_MIPS
#endif

#ifdef TO_LINUX
    #define HAS_POSIX_SIGNAL

    // !!! The Atronix build introduced a differentiation between
    // a Linux build and a POSIX build, and one difference is the
    // usage of some signal functions that are not available if
    // you compile with a strict --std=c99 switch:
    //
    //      http://stackoverflow.com/a/22913324/211160
    //
    // Yet it appears that defining _POSIX_C_SOURCE is good enough
    // to get it working in --std=gnu99.  Because there are some
    // other barriers to pure C99 for the moment in the additions
    // from Saphirion (such as the use of alloca()), backing off the
    // pure C99 and doing it this way for now.
    //
    // These files may not include reb-config.h as the first include,
    // so be sure to say:
    //
    //     #define _POSIX_C_SOURCE 199309L
    //
    // ...at the top of the file.

    #define PROC_EXEC_PATH "/proc/self/exe"
#endif


//* Mac OS X ********************************************************

#ifdef TO_OSX_PPC
#endif

#ifdef TO_OSX_X86
#endif

#ifdef TO_OSX_X64
#endif


//* Android *****************************************************

#ifdef TO_ANDROID_ARM
#endif

#ifdef TO_ANDROID
    #define PROC_EXEC_PATH "/proc/self/exe"
#endif


//* BSD ********************************************************

#ifdef TO_FREEBSD_X86
#endif

#ifdef TO_FREEBSD_X64
#endif

#ifdef TO_FREEBSD
    #define HAVE_PROC_PATHNAME
#endif

#ifdef TO_NETBSD
    #define PROC_EXEC_PATH "/proc/curproc/exe"
#endif

#ifdef TO_OPENBSD
#endif


//* HaikuOS ********************************************************

#ifdef TO_HAIKU
    #undef API_EXPORT
    #define API_EXPORT

    #define DEF_UINT
#endif


//* Amiga ********************************************************

// Note: The Amiga target is kept for its historical significance.
// Rebol required Amiga OS4 to be able to run, and the only
// machines that could run it had third-party add-on boards with
// PowerPC processors.  Hence stock machines like the Amiga4000
// which had a Motorola 68040 cannot built Rebol.
//
// To date, there has been no success reported in building Rebol
// for an Amiga emulator.  The last known successful build on
// Amiga hardware is dated 5-Mar-2011

#ifdef TO_AMIGA
    #define NO_DL_LIB
#endif


// Initially the debug build switches were all (default) or nothing (-DNDEBUG)
// but needed to be broken down into a finer-grained list.  This way, more
// constrained systems (like emscripten) can build in just the features it
// needs for a specific debug scenario.
//
// !!! Revisit a more organized way to inventory these settings and turn them
// on and off as time permits.
//
#if !defined(NDEBUG)
    #ifndef DEBUG_STDIO_OK // !!! TCC currently respecifying this, review
        #define DEBUG_STDIO_OK
    #endif

    #define DEBUG_HAS_PROBE
    #define DEBUG_MONITOR_SERIES
    #define DEBUG_COUNT_TICKS
    #define DEBUG_FRAME_LABELS
    #define DEBUG_UNREADABLE_VOIDS
    #define DEBUG_TRASH_MEMORY
    #define DEBUG_BALANCE_STATE

    // There is a mode where the track payload exists in all cells, making
    // them grow by 2 * sizeof(void*): DEBUG_TRACK_EXTEND_CELLS.  This can
    // tell you about a cell's initialization even if it carries a payload.
    //
    #define DEBUG_TRACK_CELLS

    // OUT_MARKED_STALE uses the same bit as ARG_MARKED_CHECKED.  But arg
    // fulfillment uses END as the signal of when no evaluations are done,
    // it doesn't need the stale bit.  The bit is cleared when evaluating in
    // an arg slot in the debug build, to make it more rigorous to know that
    // it was actually typechecked...vs just carrying the OUT_FLAG_STALE over.
    //
    #define DEBUG_STALE_ARGS

    // See debugbreak.h and REBNATIVE(c_debug_break)...useful!
    //
    #define INCLUDE_C_DEBUG_BREAK_NATIVE

    // See REBNATIVE(test_librebol)
    //
    #define INCLUDE_TEST_LIBREBOL_NATIVE

    // !!! This was a concept that may have merit, but doesn't actually work
    // when something creates a frame for purposes of iteration where it *may*
    // or may not evaluate.  The FFI struct analysis was an example.  Hence
    // disabling it for now, but there may be value in it enough to have a
    // frame flag for explicitly saying you don't necessarily plan to call
    // the evaluator.
    //
    // ---
    //
    // Note: We enforce going through the evaluator and not "skipping out" on
    // the frame generation in case it is hooked and something like a debug
    // step wanted to see it.  Or also, if you write `cycle []` there has to
    // be an opportunity for Do_Signals_Throws() to check for cancellation
    // via Ctrl-C.)
    //
    // This ties into a broader question of considering empty blocks to be
    // places that are debug step or breakpoint opportunities, so we make
    // sure you use `do { eval } while (NOT_END(...))` instead of potentially
    // skipping that opportunity with `while (NOT_END(...)) { eval }`:
    //
    // https://github.com/rebol/rebol-issues/issues/2229
    //
    /* #define DEBUG_ENSURE_FRAME_EVALUATES */

    // !!! Checking the memory alignment is an important invariant but may be
    // overkill to run on all platforms at all times.  It requires the
    // DEBUG_CELL_WRITABILITY flag to be enabled, since it's the moment of
    // writing that is when the check has an opportunity to run.
    //
    // !!! People using MLton to compile found that GCC 4.4.3 does not always
    // align doubles to 64-bit boundaries on Windows, even when -malign-double
    // is used.  It's a very old compiler, and may be a bug.  Disable align
    // checking for GCC 4 on Windows, hope it just means slower loads/stores.
    //
    // https://stackoverflow.com/a/11110283/211160
    //
  #ifdef __GNUC__
    #if !defined(TO_WINDOWS) || (__GNUC__ >= 5) // only  least version 5
       #define DEBUG_MEMORY_ALIGN
    #endif
  #else
    #define DEBUG_MEMORY_ALIGN
  #endif
    #define DEBUG_CELL_WRITABILITY

    // Natives can be decorated with a RETURN: annotation, but this is not
    // checked in the release build.  It's assumed they will only return the
    // correct types.  This switch is used to panic() if they're wrong.
    //
    #define DEBUG_NATIVE_RETURNS

    // This check is for making sure that an ANY-WORD! that has a binding has
    // a spelling that matches the key it is bound to.  It was checked in
    // Get_Context_Core() but is a slow check that hasn't really ever had a
    // problem.  Disabling it for now, to improve debug build performance.
  #if 0
    #define DEBUG_BINDING_NAME_MATCH
  #endif

    // It can be nice to see aliases of platform pointers as if they were
    // individual bytes, through union "puns".  Though this behavior is not
    // well defined, it can be useful a lot of the time.
    //
    // https://en.wikipedia.org/wiki/Type_punning
    //
    #define DEBUG_USE_UNION_PUNS

    // Bitfields are poorly specified, and so even if it looks like your bits
    // should pack into a struct exactly, they might not.  Only try this on
    // Linux, where it has seemed to work out (MinGW64 build on Cygwin made
    // invalid REBVAL sizes with this on)
    //
    #if defined(ENDIAN_LITTLE) && defined(TO_LINUX_X64)
        #define DEBUG_USE_BITFIELD_HEADER_PUNS
    #endif

    #define DEBUG_ENABLE_ALWAYS_MALLOC
#else
    // We may want to test the valgrind build even if it's release so that
    // it checks the R3_ALWAYS_MALLOC environment variable
    //
    #if defined(INCLUDE_CALLGRIND_NATIVE)
        #define DEBUG_ENABLE_ALWAYS_MALLOC
    #endif
#endif

// System V ABI for X86 says alignment can be 4 bytes for double.  However,
// you can change this in the compiler settings.  We should either sync with
// that setting or just skip it, and assume that we do enough checking on the
// 64-bit builds.
// 
// https://stackoverflow.com/q/14893802/
//
// !!! We are overpaying for the ALIGN_SIZE if it's not needed for double,
// so perhaps it is that which should be configurable in the build settings...
//
#if defined(TO_WINDOWS_X86) || defined(TO_LINUX_X86)
    #define DEBUG_DONT_CHECK_ALIGN
#endif


// UTF-8 Everywhere is a particularly large system change, which requires
// careful bookkeeping to allow the caching of positions to work.  These
// checks are too slow to run on most builds, but should be turned on if
// any problems are seen.
//
#ifdef DEBUG_UTF8_EVERYWHERE
    #define DEBUG_VERIFY_STR_AT  // check cache correctness on every STR_AT
    #define DEBUG_SPORADICALLY_DROP_BOOKMARKS  // test bookmark absence
    #define DEBUG_BOOKMARKS_ON_MODIFY  // main routine for preserving marks
#endif

#ifdef __SANITIZE_ADDRESS__
    #ifdef CPLUSPLUS_11
        //
        // Cast checks in SER(), NOD(), ARR() are expensive--they ensure that
        // when you cast a void pointer to a REBSER, that the header actually
        // is for a REBSER (etc.)  Disable this by default unless you are
        // using address sanitizer, where you expect things to be slow.
        //
        #define DEBUG_CHECK_CASTS
    #endif

    // Both Valgrind and Address Sanitizer can provide the call stack at
    // the moment of allocation when a freed pointer is used.  This is
    // exploited by Touch_Series() to use a bogus allocation to help
    // mark series origins that can later be used by `panic()`.  However,
    // the feature is a waste if you're not using such tools.
    //
    // If you plan to use Valgrind with this, you'll have to set it
    // explicitly...only Address Sanitizer can be detected here.
    //
    #define DEBUG_SERIES_ORIGINS
#endif

#ifdef DEBUG_MEMORY_ALIGN
    #if !defined(DEBUG_CELL_WRITABILITY)
        #error "DEBUG_MEMORY_ALIGN requires DEBUG_CELL_WRITABILITY"
    #endif
    #if !defined(DEBUG_STDIO_OK)
        #error "DEBUG_MEMORY_ALIGN requires DEBUG_STDIO_OK"
    #endif
#endif


#ifdef DEBUG_TRACK_EXTEND_CELLS
    #define DEBUG_TRACK_CELLS
    #define UNUSUAL_REBVAL_SIZE // sizeof(REBVAL)*2 may be > sizeof(REBSER)
#endif


// It can be very difficult in release builds to know where a fail came
// from.  This arises in pathological cases where an error only occurs in
// release builds, or if making a full debug build bloats the code too much.
// (e.g. the JavaScript asyncify version).  A small but helpful debug
// switch does a printf of the __FILE__ and __LINE__ of fail() callsites.
//
#ifdef DEBUG_PRINTF_FAIL_LOCATIONS
    #if !defined(DEBUG_STDIO_OK)
        #error "DEBUG_PRINTF_FAIL_LOCATIONS requires DEBUG_STDIO_OK"
    #endif
#endif
