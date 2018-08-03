//
//  File: %reb-config.h
//  Summary: "General build configuration"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
// This is the first file included.  It is included by both
// reb-host.h and sys-core.h, and all Rebol code can include
// one (and only one) of those...based on whether the file is
// part of the core or in the "host".
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

    REB_DEF         - special includes, symbols, and tables

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
    #define OS_DIR_SEP '\\'         // file path separator (Thanks Bill.)

    #if (defined(_MSC_VER) && (_MSC_VER <= 1200))
        #define WEIRD_INT_64        // non-standard MSVC int64 declarations
    #else
        #define HAS_LL_CONSTS
    #endif

    #define OS_WIDE_CHAR            // REBWCHAR used strings passed to OS API
    #include <wchar.h>

    // ASCII strings to Integer
    #define ATOI                    // supports it
    #define ATOI64                  // supports it
    #define ITOA64                  // supports it

    #define HAS_ASYNC_DNS           // supports it

    #define NO_TTY_ATTRIBUTES       // used in read-line.c

    // Used when we build REBOL as a DLL:
    #define API_EXPORT __declspec(dllexport)
    #define API_IMPORT __declspec(dllimport)

    #define WIN32_LEAN_AND_MEAN     // trim down the Win32 headers
#else
    #define OS_DIR_SEP '/'          // rest of the world uses it

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


//* Mac OS/X ********************************************************

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
    //
    // Note to anyone porting to Amiga: it has BOOL which could be used
    // for REBOOL
    //
    #define HAS_SMART_CONSOLE
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
    #define DEBUG_COUNT_TICKS
    #define DEBUG_FRAME_LABELS
    #define DEBUG_UNREADABLE_BLANKS
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
    // Get_Var_Core() but is a slow check that hasn't really ever had a
    // problem.  Disabling it for now, to improve debug build performance.
  #if 0
    #define DEBUG_BINDING_NAME_MATCH
  #endif

    // Cast checks in SER(), NOD(), ARR() are expensive--they make sure that
    // when you have a void pointer and cast it to a REBSER, that the header
    // actually is for a REBSER (etc.)  Disable this by default unless you are
    // using address sanitizer, where you expect your executable to be slow.
    //
    #ifdef __SANITIZE_ADDRESS__
        #define DEBUG_CHECK_CASTS

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

        // The chunk stack has some bigger checks which are good to have on
        // when it's being modified.
        //
        #define DEBUG_CHUNK_STACK
    #endif

    // !!! Due to the massive change of UTF8-Everywhere, it motivates some
    // particularly strong checks.
    //
    #define DEBUG_UTF8_EVERYWHERE
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
