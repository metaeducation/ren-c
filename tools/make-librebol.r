REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make files related to the external API (for %rebol.h)"
    File: %make-librebol.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2024 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

if trap [:import/into] [  ; See %import-shim.r
    do load append copy system/script/path %import-shim.r
]

import <bootstrap-shim.r>

import <common.r>
import <common-parsers.r>
import <common-emitter.r>

print "--- Make Reb-Lib Headers ---"

args: parse-args system/script/args  ; either from command line or DO/ARGS

; Assume we start up in the directory where we want build products to go
;
prep-dir: join what-dir %prep/

mkdir/deep join prep-dir %include/
mkdir/deep join prep-dir %core/

ver: load-value join repo-dir %src/boot/version.r


=== "PROCESS %a-lib.h TO PRODUCE DESCRIPTION OBJECTS FOR EACH API" ===

; This leverages the prototype parser, which uses PARSE on C lexicals, and
; loads Rebol-structured data out of comments in the file.
;
; Currently only %a-lib.c is searched for API entries.  This makes it
; easier to track the order of the API routines and change them sparingly
; (such as by adding new routines to the end of the list, so as not to break
; binary compatibility with code built to the old ordered interface).  The
; point of needing that stability hasn't been reached yet, but will come.
;
; !!! Having the C parser doesn't seem to buy us as much as it sounds, as
; this code has to parse out the types and parameter names.  Is there a way
; to hook it to get this information?

api-objects: make block! 50

map-each-api: func [code [block!]] [  ; lambda bootstrap doesn't support LET
    return map-each api api-objects compose [  ; compose so bootstrap sees 'API
        let aux: make object! compose [break: (^break) continue: (^continue)]
        eval overbind aux overbind api (code)
    ]
]

for-each-api: func [code [block!]] [  ; lambda bootstrap doesn't support LET
    return for-each api api-objects compose [  ; compose so bootstrap sees 'API
        let aux: make object! compose [break: (^break) continue: (^continue)]
        eval overbind aux overbind api (code)
    ]
]

emit-proto: func [return: [~] proto] [
    header: proto-parser/data

    all [
        block? header
        2 <= length of header
        set-word? header/1
    ] else [
        fail [
            proto
            newline
            "Prototype has bad Rebol function header block in comment"
        ]
    ]

    if header/2 != 'API [return ~]
    if not set-word? header/1 [
        fail ["API declaration should be a SET-WORD!, not" (header/1)]
    ]

    paramlist: collect [
        parse2 proto [
            copy return-type to "API_" "API_" copy name to "(" skip
            ["void)" | some [  ; C void, or at least one parameter expected
                [copy param to "," skip | copy param to ")" to end] (
                    ;
                    ; Separate type from parameter name.  Step backwards from
                    ; the tail to find space, or non-letter/digit/underscore.
                    ;
                    trim/head/tail param
                    identifier-chars: charset [
                        #"A" - #"Z"
                        #"a" - #"z"
                        #"0" - #"9"
                        #"_"

                        ; #"." in variadics (but all va_list* in API defs)
                    ]
                    pos: back tail param
                    while [pick identifier-chars pos/1] [
                        pos: back pos
                    ]
                    keep trim/tail copy/part param next pos  ; TEXT! of type
                    keep to word! next pos  ; WORD! of the parameter name
                )
            ]]
        ] else [
            fail ["Couldn't extract API schema from prototype:" proto]
        ]
    ]

    if (to set-word! name) != header/1 [  ; e.g. `//  rebValue: API`
        fail [
            "Name in comment header (" header/1 ") isn't C function name"
            "minus API_ prefix to match" (name)
        ]
    ]

    if is-variadic: did find paramlist 'vaptr [
        parse2 paramlist [  ; Note: block! parsing
            ;
            ; Any generalized "modes" or "flags" should come first, which
            ; facilitates C99 macros that want two places to splice arguments:
            ; head and tail, e.g. this was once done with `quotes`
            ;
            ;     #define rebFoo(...) API_rebFoo(0, __VA_ARGS__, rebEND)
            ;     #define rebFooQ(...) API_rebFoo(1, __VA_ARGS__, rebEND)
            ;
            ; But now, it's needed for passing in the specifier.

            "RebolSpecifier**" 'specifier_ref

            copy paramlist: to "const void*"  ; signal start of variadic

            "const void*" 'p
            "void*" 'vaptr
        ] else [
            fail [name "has unsupported variadic paramlist:" mold paramlist]
        ]
    ]

    ; Note: Cannot set object fields directly from R3-Alpha PARSE in Bootstrap
    ; https://github.com/rebol/rebol-issues/issues/2317
    ;
    append api-objects make object! compose [
        spec: match block! third header  ; Rebol metadata API comment
        name: (ensure text! name)
        return-type: (ensure text! trim/tail return-type)
        paramlist: (ensure block! paramlist)
        proto: (ensure text! proto)
        is-variadic: (reify-logic is-variadic)
    ]
]

process: func [return: [~] file] [
    proto-parser/file: file
    proto-parser/emit-proto: :emit-proto
    proto-parser/process as text! read file
]

src-dir: join repo-dir %src/core/

process (join src-dir %a-lib.c)


=== "GENERATE LISTS USED TO BUILD REBOL.H" ===

; For readability, the technique used is not to emit line-by-line, but to
; give a "big picture overview" of the header file.  It is substituted into
; like a conventional textual templating system.  So blocks are produced for
; long generated lists, and then spliced into slots in that "big picture"

extern-prototypes: map-each-api [
    cscape [:api {RL_API $<Proto>}]
]

lib-struct-fields: map-each-api [
    cfunc-params: delimit ", " compose [
        (if is-variadic ["RebolSpecifier** specifier_ref"])
        (spread map-each [type var] paramlist [spaced [type var]])
        (if is-variadic [
            spread ["const void* p" "void* vaptr"]
        ])
    ]
    cfunc-params: default ["void"]
    cscape [:api
        {$<Return-Type> (*$<Name>)($<Cfunc-Params>)}
    ]
]

non-variadic-api-macros: map-each-api [
    if not is-variadic [
        cscape [:api {#define $<Name> LIBREBOL_PREFIX($<Name>)}]
    ]
]

variadic-api-c-helpers: copy []
variadic-api-c++-helpers: copy []

for-each-api [
    if not is-variadic [
        continue
    ]

    all [
        spec
        find spec #noreturn
    ] then [
        assert [return-type = "void"]
        attributes: "ATTRIBUTE_NO_RETURN  /* divergent */"
        epilogue: cscape [:api
            "DEAD_END;  /* $<Name>() never returns */"
        ]
    ] else [
        attributes: null
        epilogue: null
    ]

    helper-params: map-each [type var] paramlist [
        spaced [type var]
    ]
    proxied-args: map-each [type var] paramlist [
        to-text var
    ]

    return-keyword: if return-type != "void" ["return "] else [null]

    append variadic-api-c-helpers cscape [:api {
        $<Maybe Attributes>
        static inline $<Return-Type> $<Name>_helper(  /* C version */
            RebolSpecifier** specifier_ref,
            $<Helper-Params, >
            const void* p, ...
        ){
            va_list va;
            va_start(va, p);  /* $<Name>() calls va_end() */

            $<maybe return-keyword >LIBREBOL_PREFIX($<Name>)(
                specifier_ref,  /* pointer-to-pointer, may update variable */
                $<Proxied-Args, >
                p, &va  /* non-null vaptr means p is first item */
            );
            $<Maybe Epilogue>
        }
    }]

    append variadic-api-c++-helpers cscape [:api {
        template <typename... Ts>
        $<Maybe Attributes>
        inline $<Return-Type> $<Name>_helper(  /* C++ version */
            RebolSpecifier** specifier_ref,
            $<Helper-Params, >
            const Ts & ...args
        ){
            const void* p[sizeof...(args)];  /* includes rebEND */
            rebVariadicPacker_internal(0, p, args...);

            $<maybe return-keyword >LIBREBOL_PREFIX($<Name>)(
                specifier_ref,  /* pointer-to-pointer, may be updated */
                $<Proxied-Args, >
                p, nullptr  /* null vaptr means p is array of items */
            );
            $<Maybe Epilogue>
        }
    }]
]

variadic-api-specifier-capturing-macros: map-each-api [
    if is-variadic [
        fixed-params: map-each [type var] paramlist [
            to-text var
        ]

        cscape [:api {
            #define $<Name>($<Fixed-Params,>...) \
                $<Name>_helper( \
                    LIBREBOL_SPECIFIER,  /* captured from callsite! */ \
                    $<Fixed-Params, >__VA_ARGS__, rebEND \
                )
        }]
    ]
]

variadic-api-run-in-lib-macros: map-each-api [
    if is-variadic [
        fixed-params: map-each [type var] paramlist [
            to-text var
        ]

        cscape [:api {
            #define $<Name>Core($<Fixed-Params,>...) \
                $<Name>_helper( \
                    0,  /* just run in lib */ \
                    $<Fixed-Params, >__VA_ARGS__, rebEND \
                )
        }]
    ]
]

variadic-api-c89-alias-macros: map-each-api [
    if is-variadic [
        cscape [:api {#define $<Name>_c89 $<Name>_helper}]
    ]
]


=== "REMOVE TERMINAL NEWLINES FROM API SETS" ===

; We don't want to write the API template like this:
;
;     #if SOME_DEFINED_THING  /* space off by one newline */
;
;         $[List-Of-Stuff]
;     #endif  /* no newline, since List-Of-Stuff has newline on each entry */
;
; So drop the terminal newline, permitting:
;
;     #if SOME_DEFINED_THING
;
;         $[List-Of-Stuff]
;
;     #endif
;

assert [newline = take/last last variadic-api-c-helpers]
assert [newline = take/last last variadic-api-c++-helpers]
assert [newline = take/last last variadic-api-specifier-capturing-macros]
assert [newline = take/last last variadic-api-run-in-lib-macros]


=== "GENERATE REBOL.H" ===

; Rather than put too many comments here in the Rebol, err on the side of
; putting comments in the header itself.  `/* use old C style comments */`
; to help cue readers to knowing they're reading generated code and don't
; edit, since the Rebol codebase at large uses `//`-style comments.

e-lib: make-emitter "Rebol External Library Interface" (
    join prep-dir %include/rebol.h
)

e-lib/emit [ver {
    #ifndef REBOL_H_1020_0304  /* "include guard" allows multiple #includes */
    #define REBOL_H_1020_0304  /* numbers in case REBOL_H defined elsewhere */


    /*
     * API #DEFINE OPTIONS
     *
     * The following options are available before you #include "rebol.h"
     *
     *   LIBREBOL_USE_C89         Expose functions like rebValue_c89(...)
     *
     *   LIBREBOL_NO_CPLUSPLUS    Disable conversions like rebValue("1 +", 2);
     *
     *   LIBREBOL_NO_STDLIB       Suppress inclusion of <stdlib.h>
     *   LIBREBOL_NO_STDINT       Suppress inclusion of <stdint.h>
     *   LIBREBOL_NO_STDBOOL      Suppress inclusion of <stdbool.h>
     *
     *   LIBREBOL_SPECIFIER       Variable name variadics implicitly capture
     *
     *   LIBREBOL_USES_API_TABLE  No plain linker access to API exports
     *
     * For more details on each, read the comments further down in this file.
     */


    /*
     * API VERSION
     *
     * These constants are part of an old Rebol versioning system that hasn't
     * been paid much attention to:
     *
     *   http://rebol.com/release-archive.html
     *
     * Keeping as a placeholder.
     */

    #define LIBREBOL_VERSION        $<ver/1>
    #define LIBREBOL_MAJOR          $<ver/2>
    #define LIBREBOL_MINOR          $<ver/3>


    /*
     * LIBREBOL_USE_C89 option
     *
     * As a sort of guiding principle against complexity, Rebol has tried as
     * hard as possible to be able to build and run as pure ANSI C89.  The
     * need for variadic macros, '//'-style comments, and declaring variables
     * in mid-function meant C99 was ultimately required.
     *
     * HOWEVER, the API can still be used from C89, albeit the variadic APIs
     * become more awkward.
     *
     * For instance, instead of:
     *
     *      RebolValue* x = rebValue("10");
     *      RebolValue* y = rebValue(one, "+", rebI(20));
     *
     * You have to use the internal functions without the variadic wrappers,
     * making you responsible for the termination signal:
     *
     *      RebolValue* x = rebValue_c89(NULL, "10", rebEND);
     *      RebolValue* y = rebValue_c89(NULL, one, "+", rebI(20), rebEND);
     *
     * The first parameter is the environment in which the code will be
     * looked up, which defaults to running in an isolated context inheriting
     * from LIB if null.  Note that APIs with a fixed number of parameters
     * (such as rebI()) do not have a distinct `_c89` version.
     *
     * At time of writing, there are no clients of this API.  But it is kept
     * in the spirit of being careful about dependency control, to help count
     * exactly how many steps away from baseline the system is making.
     */

    #if !defined(LIBREBOL_USE_C89)
        #define LIBREBOL_USE_C89 0
    #endif


    /*
     * TRIGGER UP-FRONT ERROR IF COMPILER LACKS __VA_ARGS__ FEATURE
     *
     * C99 and C++11 standardize an interface for variadic macros:
     *
     * https://stackoverflow.com/questions/4786649/
     *
     * If not using the C89 manual calls, this feature is a requirement.
     *
     * We'd like to give a targeted error message if the feature is missing,
     * vs. have the compiler spew out gibberish that is harder to decipher.
     * But since some pre-C99 compilers actually support __VA_ARGS__, don't
     * use the compiler version to preemptively error.  Instead, try to call
     * a dummy variadic macro...which will lead users to this spot if it
     * does not work.
     */

    #if (! LIBREBOL_USE_C89)
        #define Compiler_Lacks_Variadic_Macros_If_This_Errors(...) \
            (__VA_ARGS__ + 304)

        static inline int Feature_Test_Compiler_For_Variadic_Macros(void)
            { return Compiler_Lacks_Variadic_Macros_If_This_Errors(1020); }
    #endif


    /*
     * LIBREBOL_NO_CPLUSPLUS option
     *
     * Some features are enhanced by the presence of a C++11 compiler or
     * above.  This includes allowing `int` or `std::string` parameters to
     * APIs instead of using `rebI()` or `rebT()`.  These are on by default
     * if the compiler is capable, but can be suppressed.
     *
     * (Note that there's still some conditional code based on __cplusplus
     * even when this switch is used, but not in a way that affects the
     * runtime behavior uniquely beyond what C99 would do.)
     */

    #if !defined(LIBREBOL_NO_CPLUSPLUS)  /* define before including rebol.h */
        #if LIBREBOL_USE_C89
            #define LIBREBOL_NO_CPLUSPLUS 1
        #elif defined(__cplusplus) && __cplusplus >= 201103L
            /* C++11 or above, if following the standard (VS2017 does not) */
            #define LIBREBOL_NO_CPLUSPLUS 0
        #elif defined(CPLUSPLUS_11) && CPLUSPLUS_11
            /* Custom C++11 or above flag, to override Visual Studio's lie */
            #define LIBREBOL_NO_CPLUSPLUS 0
        #else
            #define LIBREBOL_NO_CPLUSPLUS 1  /* compiler not current enough */
        #endif
    #endif


    /*
     * ALLOW SUPPRESSING INCLUSION OF STDLIB.H, STDINT.H, STDBOOL.H
     *
     * The goal is to make it possible that the only include file one needs
     * to make a simple Rebol library client is `#include "rebol.h"`.
     * Accomplishing that requires including a few headers.
     *
     * Some special compilation scenarios require not assuming any headers are
     * available on the platform.  This lets you disable those automatic
     * inclusions if you need to.
     *
     * If you are using C89 for some reason, "pstdbool.h" and "pstdint.h" are
     * available on the internet as shims for <stdbool.h> and <stdint.h>
     */

    #if !defined(LIBREBOL_NO_STDLIB)  /* definable before including rebol.h */
        #define LIBREBOL_NO_STDLIB 0
    #endif
    #if !defined(LIBREBOL_NO_STDINT)
        #define LIBREBOL_NO_STDINT LIBREBOL_USE_C89
    #endif
    #if !defined(LIBREBOL_NO_STDBOOL)
        #define LIBREBOL_NO_STDBOOL LIBREBOL_USE_C89
    #endif

    #if LIBREBOL_NO_STDLIB
        /*
         * This file won't compile without definitions for uintptr_t and
         * bool, so make those as a minimum.  They have to be binary compatible
         * with how the library was compiled...these are just guesses.
         */
        #define int64_t long long  /* used for integers */
        #define uint32_t unsigned int  /* used for codepoint (use int?) */
        #define uintptr_t unsigned int  /* used for ticks */
        #define bool _Bool  /* actually part of C99 compiler */
    #else
        #include <stdlib.h>  /* for size_t */
        #if !defined(_PSTDINT_H_INCLUDED) && (! LIBREBOL_NO_STDINT)
            #include <stdint.h>  /* for uintptr_t, int64_t, etc. */
        #endif
        #if !defined(_PSTDBOOL_H_INCLUDED) && (! LIBREBOL_NO_STDBOOL)
            #if !defined(__cplusplus)
                #include <stdbool.h>  /* for bool, true, false (if C99) */
            #endif
        #endif
    #endif

    /*
     * !!! Needed by following two macros.
     */
    #ifndef __has_builtin
        #define __has_builtin(x) 0
    #endif
    #if !defined(GCC_VERSION_AT_LEAST)  /* !!! duplicated in %c-enhanced.h */
        #ifdef __GNUC__
            #define GCC_VERSION_AT_LEAST(m, n) \
                (__GNUC__ > (m) || (__GNUC__ == (m) && __GNUC_MINOR__ >= (n)))
        #else
            #define GCC_VERSION_AT_LEAST(m, n) 0
        #endif
    #endif

    /*
     * !!! _Noreturn was introduced in C11, but prior to that (including C99)
     * there was no standard way of doing it.  If we didn't mark APIs which
     * don't return with this, there'd be warnings in the calling code.
     *
     * 1. TCC added a _Noreturn and noreturn in 2019 (at first doing nothing,
     *    but then got an implementation).  Unfortunately they haven't bumped
     *    the version reported by __TINYC__ since 2017, so there's no easy
     *    detection of the availability (and TCC apt packages can be old).
     *    So use newer TCCs in C11 mode or do `-DATTRIBUTE_NO_RETURN=_Noreturn`
     */
    #if !defined(ATTRIBUTE_NO_RETURN)
        #if defined(__clang__) || GCC_VERSION_AT_LEAST(2, 5)
            #define ATTRIBUTE_NO_RETURN __attribute__ ((noreturn))
        #elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
            #define ATTRIBUTE_NO_RETURN _Noreturn
        #elif defined(__TINYC__)
            #define ATTRIBUTE_NO_RETURN  /* _Noreturn unreliable [1] */
        #elif defined(_MSC_VER)
            #define ATTRIBUTE_NO_RETURN __declspec(noreturn)
        #else
            #define ATTRIBUTE_NO_RETURN
        #endif
    #endif

    /*
     * !!! Same story for DEAD_END as for ATTRIBUTE_NO_RETURN.  Necessary to
     * suppress spurious warnings.
     */
    #if !defined(DEAD_END)  /* !!! duplicated in %reb-config.h */
        #if __has_builtin(__builtin_unreachable) || GCC_VERSION_AT_LEAST(4, 5)
            #define DEAD_END __builtin_unreachable()
        #elif defined(_MSC_VER)
            #define DEAD_END __assume(0)
        #else
            #define DEAD_END
        #endif
    #endif


    /*
     * The API can be used by the core on value cell pointers that are in
     * stable locations guarded by GC (e.g. frame argument or output cells).
     * Since the core uses Value*, it must be accurate (not just a void*)
     */

    struct RebolValueStruct;
    typedef struct RebolValueStruct RebolValue;


    /*
     * VARIADIC API "INSTRUCTIONS"
     *
     * The functions that are named rebXXX with XXX in all caps are designed
     * for passing in variadic argument streams.  This means things like
     * rebRELEASING() a.k.a. rebR(), or rebQUOTING() a.k.a. rebQ().  They
     * return "instructions" that are not RebolValue*, and are only for use
     * in the variadic calls.
     *
     * You should not cache references to them in variables, but only pass
     * them directly in the variadic stream.  This is because the feeding of
     * the va_list in case of error is the only way they are cleaned up.
     */

    struct RebolNodeStruct;
    typedef struct RebolNodeStruct RebolNodeInternal;


    /*
     * SPECIFIER TYPE (internal)
     *
     * Specifiers are an evolving idea, which represent "binding environments"
     * for looking up values.  They are a chain of contexts...such as a
     * FRAME! for a function, inheriting from the module in which that
     * function was defined.  There's not currently an exposed datatype in
     * the language for specifiers, so the only currency for them are BLOCK!
     * or other arrays that have captured the specifier as their binding.
     *
     * The API needs to speak in terms of specifiers in order to know where
     * to bind the source code that's scanned in variadic calls.  So there is
     * some exposure of the type, but it's not meant to be manipulated
     * directly by API clients at this time.  Instead, specifiers are captured
     * implicitly by the macros that implement variadic API functions.
     */

    typedef struct RebolNodeStruct RebolSpecifier;


    /*
     * LEVEL TYPE (internal)
     *
     * Levels are the C structure backing the state of a FRAME! that is in the
     * running state.  A pointer to a level is what gets received by a native
     * from which things like the arguments to an invocation can be extracted.
     */

    struct RebolLevelStruct;
    typedef struct RebolLevelStruct RebolLevel;


    /*
     * REBOL BOUNCE
     *
     * A "Bounce" is the return result from a native.  It can be a RebolValue
     * in the case of being the final resolution of the return result.  Or it
     * can be a signal of a continuation (or delegation) of code.
     */

    typedef void* RebolBounce;


    /*
     * `wchar_t` is a pre-Unicode abstraction, whose size varies per-platform
     * and should be avoided where possible.  But Win32 standardizes it to
     * 2 bytes in size for UTF-16, and uses it pervasively.  So libRebol
     * currently offers APIs (e.g. rebTextWide() instead of rebText()) which
     * support this 2-byte notion of wide characters.
     *
     * In order for C++ to be type-compatible with Windows's WCHAR definition,
     * a #define on Windows to wchar_t is needed.  But on non-Windows, it
     * must use `uint16_t` since there's no size guarantee for wchar_t.  This
     * is useful for compatibility with unixodbc's SQLWCHAR.
     *
     * !!! REBWCHAR is just for the API definitions--don't mention it in
     * client code.  If the client code is on Windows, use WCHAR.  If it's in
     * a unixodbc client use SQLWCHAR.  But use UTF-8 if you possibly can.
     */

    #if LIBREBOL_NO_STDINT
        #define REBWCHAR unsigned int
    #elif defined(_WIN32)  /* _WIN32 is all Windows, _WIN64 only if 64-bit */
        #define REBWCHAR wchar_t
    #else
        #define REBWCHAR uint16_t
    #endif


    /*
     * "Dangerous Function" which is called by rebRescue().  Argument can be a
     * RebolValue* but does not have to be.
     *
     * Result must be a RebolValue* or nullptr.
     *
     * !!! If the dangerous function returns an ERROR!, it will currently be
     * converted to null, which parallels TRAP without a handler.  nulls will
     * be converted to voids.
     */

    typedef RebolValue* (REBDNG)(void *opaque);


    /*
     * "Rescue Function" called as the handler in rebRescueWith().  Receives
     * the RebolValue* of the error that occurred, and the opaque pointer.
     *
     * !!! If either the dangerous function or the rescuing function return an
     * ERROR! value, that is not interfered with the way rebRescue() does.
     */

    typedef RebolValue* (REBRSC)(RebolValue* error, void *opaque);


    /*
     * For some HANDLE!s GC callback.  Note that because these cleanups are
     * called during recycling, they cannot run most API routines.  Some
     * exceptions are made for extracting handle properties and running
     * rebFree() functions, but they have to be managed carefully.
     *
     * !!! It may be common enough to want to defer some code until after
     * the GC and have a list to process that this be offered as a service.
     * Users have a problem implementing such a service themselves in terms
     * of knowing when the GC is.  Though since a GC can happen at any time,
     * this might create some unpredictable nesting.
     */

    typedef void (CLEANUP_CFUNC)(const RebolValue*);


    /*
     * The API maps Rebol's ~null~ "antiform" to C's 0 pointer, **but don't
     * use C's NULL macro for that**.
     *
     * The reason is that some C compilers define NULL as simply the integer
     * constant 0, which breaks use with variadic APIs...as they will interpret
     * it as an integer and not a pointer.
     *
     * **It's clearest to use C++'s `nullptr`**, or a suitable C shim for it,
     * e.g. `#define nullptr ((void*)0)`.  That helps avoid making it seem
     * like rebNull is some distinct concept which can't safely be assumed
     * as exactly equivalent to C's null.
     *
     * However, **using NULL is broken, so don't use it**.  This macro is
     * provided in case defining `nullptr` is not an option--for some reason.
     */

    #define rebNull \
        ((RebolValue*)0)



    #ifdef __cplusplus
    extern "C" {
    #endif


    /*
     * Function entry points for librebol.  Formulating this way allows the
     * interface structure to be passed from an EXE to a DLL, then the DLL
     * can call into the EXE (which is not generically possible via linking).
     */

    struct RebolApiTableStruct {
        $[Lib-Struct-Fields];
    };
    typedef struct RebolApiTableStruct RebolApiTable;



    #ifdef LIBREBOL_USES_API_TABLE  /* can't direct call into EXE entry points */
        /*
        * We will translate rebXXX() into g_librebol->rebXXX()
        */
        extern RebolApiTable *g_librebol;  /* passed to the DLL init function */

        #define LIBREBOL_PREFIX(api_name) g_librebol->api_name

    #else  /* ...calling Rebol as DLL, or code built into the EXE itself */

        /*
         * !!! The RL_API macro has to be defined for the external prototypes
         * to compile.  Assume for now that if not defined via %reb-config.h,
         * then it can be empty--though this will almost certainly need to
         * be revisited (as it needs __dllimport and other such things on
         * Windows, so those details will come up at some point)
         */
      #if !defined(RL_API)
        #define RL_API
      #endif

        #define LIBREBOL_PREFIX(api_name) API_##api_name

        /*
         * Extern prototypes for API_XXX, don't call these functions directly.
         * They use vaptr instead of `...`, and may not do all the proper
         * exception/longjmp handling needed.
         */

        $[Extern-Prototypes];

    #endif  /* (! LIBREBOL_USES_API_TABLE) */

    #ifdef __cplusplus
    }  /* end the extern "C" */
    #endif


    /*
     * NON-VARIADIC API ENTRY POINT MACROS
     *
     * When the user writes `rebInteger(i)` it needs a macro to wrap it even
     * though it's not variadic.  Because if it's being compiled by the core
     * that needs to resolve as:
     *
     *     rebInteger(i) => API_rebInteger(i)
     *
     * And if it's being compiled against the API table it needs to be:
     *
     *     rebInteger(i) => g_librebol->rebInteger(i)
     *
     * So these macros accomplish that using the pattern:
     *
     *      #define rebInteger LIBREBOL_PREFIX(rebInteger)
     */

    $[Non-Variadic-Api-Macros]


    /*
     * NON-VARIADIC API SHORTHANDS
     *
     * These shorthand macros make the API somewhat more readable, but as
     * they are macros you can redefine them to other definitions if you want.
     *
     * THESE DON'T WORK IN JAVASCRIPT, so when updating them be sure to update
     * the JavaScript versions, which have to make ordinary stub functions.
     * (The C portion of the Emscripten build can use these internally, as
     * the implementation is C.  But when calling the lib from JS, it is
     * obviously not reading this generated header file!)
     *
     * Note: Must be defined before the variadic helpers so that to_rebarg()
     * C++ converters for recursive variadic destructuring can use them.
     */

    #define rebR rebRELEASING

    #define rebT(utf8) \
        rebR(rebText(utf8))  /* might rebTEXT() delayed-load? */

    #define rebI(int64) \
        rebR(rebInteger(int64))

    #define rebL(flag) \
        rebR(rebLogic(flag))

    #define rebQ rebQUOTING
    #define rebU rebUNQUOTING


    /*
     * VARIADIC API END SIGNAL
     *
     * In C, `va_list` is the only way to take a variadic number of arguments.
     *
     *   https://en.cppreference.com/w/c/variadic/va_list
     *
     * It is very low-level, has no typechecking, and does not offer a count
     * of the number of arguments passed.  So something needs to be passed
     * that signals the end.
     *
     * Since a C nullptr (pointer cast of 0) is used to represent the Rebol
     * `null` in the API, something different must be used to indicate the
     * end of variadic input.  So a *pointer to data* is used where the first
     * byte of that data is 192 (END_SIGNAL_BYTE)--illegal in UTF-8 sequences.
     *
     * To Rebol, the first bit being 1 means it's a Rebol node, the second
     * being 1 mean it is in the "free" state.  The low bit in the first byte
     * set suggests it points to a "series"...though it doesn't (this helps
     * prevent code from trying to write a cell into a rebEND signal).
     *
     * The second byte is 0, coming from the '\0' terminator of the C string
     * literal.  This isn't strictly necessary, as the 192 is enough to know
     * it's not a Cell, Series stub, or UTF8.  But it can guard against
     * interpreting garbage input as rebEND, as the sequence {192, 0} is less
     * likely to occur at random than {192, ...}.  And leveraging a literal
     * form means we don't need to define a single byte somewhere to then
     * point at it.
     *
     * Note: This is for internal use only unless LIBREBOL_USE_C89.  But even
     * without that, calling it `rebEND_internal` would affect readability
     * of the implementation code enough to not be worth it.
     */

    #define rebEND "\xC0"


    /*
     * PREDEFINED C++ ARGUMENT CONVERSION FUNCTIONS
     *
     * When built as C++, the argument list to variadic APIs is destructured
     * using variadic templates, allowing each argument to do typechecking
     * (vs. the completely type-unsafe va_list of C).
     *
     * As an added bonus, the processing of the arguments at compile time
     * permits arbitrary transformations.  This means things like `int` can
     * be turned into rebI(...) to produce a RebolValue, instead of giving
     * an invalid type error.
     *
     * These converters are predefined, but you can add your own, like this
     * one for converting std::string to TEXT!:
     *
     *    #include <string>
     *
     *    inline const void* to_rebarg(const std::string &text)
     *      { return rebT(text.c_str()); }
     *
     * (It's not predefined to avoid forcing inclusion of <string>, but it
     * is easy to add if you want it.)
     */

    #if (! LIBREBOL_NO_CPLUSPLUS)

        #include <cstddef>  /* for std::nullptr_t */

        inline const void* to_rebarg(std::nullptr_t val)
            { return val; }

        inline const void* to_rebarg(const RebolValue* val)
            { return val; }

        inline const void* to_rebarg(const RebolNodeInternal* instruction)
            { return instruction; }

        inline const void* to_rebarg(const char *source)
            { return source; }  /* not TEXT!, but LOADable source code */

        inline const void* to_rebarg(bool b)
            { return rebL(b); }

        inline const void* to_rebarg(int i)
            { return rebI(i); }

        inline const void* to_rebarg(double d)
            { return rebR(rebDecimal(d)); }

    #endif


    /*
     * VARIADIC API HELPERS
     *
     * These helpers are called by variadic macros.  Those macros pass the
     * parameters they receive with a specifier at the beginning and `rebEND`
     * tacked on in the final position.  (See rebEND above for why.)
     *
     * As with non-variadic API entry points, these translate a raw name like
     * `rebValue()` to either `API_rebValue()` or `g_librebol->rebValue()`.
     * But it also translates the variadic arguments into two pointers
     * called `p` and `vaptr`, that can be passed to the function pointers in
     * the API table
     *
     * If C, then `p` is passed as the pointer to the first element in the
     * variadic sequence (which is actually a fixed argument that's not part
     * of the `...` list).  Then `vaptr` is passed as a pointer to va_list
     * given by va_start().
     *
     * If building with C++, all the arguments (including the first) are packed
     * into an array whose size is known at compile-time, and the pointer to
     * that array is passed as `p`.  Then `vaptr` is passed as null, indicating
     * to the internals that the arguments are in this alternate format.
     *
     * The reason for supporting two alternate implementations is that the C++
     * version is significantly more powerful.  Not only can it type-check
     * the variadic arguments, it can offer hooks to convert C++ values into
     * Rebol values.  (See the Argument Conversion Functions above).
     *
     * 1. In the spirit of dependency control, we only include <stdarg.h> if
     *    actually using the C variadic method.  This means the API table
     *    uses `void* vaptr` instead of `va_list* vaptr`.
     */

    #if LIBREBOL_NO_CPLUSPLUS

        #include <stdarg.h>  /* only included in C builds [1] */

        $[Variadic-Api-C-Helpers]

    #else

        template <typename Last>
        void rebVariadicPacker_internal(
            int i,
            const void* p[],  /* packed array of arguments */
            const Last &last
        ){
            p[i] = to_rebarg(last);
        }

        template <typename First, typename... Rest>
        void rebVariadicPacker_internal(
            int i,
            const void* p[],  /* packed array of arguments */
            const First& first, const Rest& ...rest
        ){
            p[i] = to_rebarg(first);
            rebVariadicPacker_internal(i + 1, p, rest...);
        }

        $[Variadic-Api-C++-Helpers]

    #endif


    /*
     * VARIADIC API SPECIFIER CAPTURING MACROS
     *
     * LIBREBOL_SPECIFIER defines the name of the variable which will be
     * sneakily picked up by these variadic API macros, in order to provide a
     * binding context that is relevant.  e.g. if you're inside a native, then
     * the context should be for that native's function parameters, chained to
     * the module, then inheriting from lib.
     *
     * Getting this inheritance is tricky.  It means there has to be a global
     * relevant definition for when you're calling a subroutine that's not
     * in a native, and it means the INCLUDE_PARAMS_OF_XXX has to override
     * that same name with a new variable implicating the function.
     *
     * What the name of the variable is needs to vary by module and case, so
     * this does that.  But if you don't specify it at all, then it means
     * the API execution will be done in its own isolated environment that
     * just inherits from lib.
     */

    #if (! LIBREBOL_USE_C89)

        #if !defined(LIBREBOL_SPECIFIER)
            #define LIBREBOL_SPECIFIER 0  /* nullptr may not be available */
        #endif

        $[Variadic-Api-Specifier-Capturing-Macros]

    #endif  /* (! LIBREBOL_USE_C89) */


    /*
     * VARIADIC API RUN IN LIB MACROS
     *
     * Variant that just runs the code in lib.
     */

    #if (! LIBREBOL_USE_C89)

        $[Variadic-Api-Run-In-Lib-Macros]

    #endif  /* (! LIBREBOL_USE_C89) */


    /*
     * TYPE-SAFE rebUnboxHandle() MACRO VARIANTS
     */

    #if (! LIBREBOL_USE_C89)
        #define rebUnboxHandle(TP,...) \
            ((TP)rebUnboxHandleCData((size_t*)(0), __VA_ARGS__))

        #define rebUnboxHandleCore(TP,...) \
            ((TP)rebUnboxHandleCDataCore((size_t*)(0), __VA_ARGS__))
    #endif


    /*
     * ALIASED NAMES OF THE HELPER FUNCTIONS IF USING C89
     *
     * The "C89 API" is really just requiring you to call the C helper
     * functions directly for variadic APIs.  But to make it clearer and
     * shorter to type, the helpers are aliased as `rebXXX_c89`.
     *
     * (Note that naming the C helpers `rebXXX_c89` in the first place would
     * not give the desired polymorphism of having the wrapping macros work
     * with either C++ or C...unless you named the C++ helpers `rebXXX_c89`
     * as well, which is unnecessarily misleading.)
     */

    #if LIBREBOL_USE_C89

        $[Variadic-Api-C89-Alias-Macros]

    #endif  /* LIBREBOL_USE_C89 */


    /*
     * TYPE-SAFE rebAlloc() MACRO VARIANTS
     *
     * rebAllocBytes() offers some advantages over hosts just using malloc():
     *
     *  1. Memory can be retaken to act as a BINARY! series without another
     *     allocation, via rebRepossess().
     *
     *  2. Memory is freed automatically in the case of a failure in the
     *     frame where the rebAlloc() occured.  This is especially useful
     *     when mixing C code involving allocations with rebValue(), etc.
     *
     *  3. Memory gets counted in Rebol's knowledge of how much memory the
     *     system is using, for the purposes of triggering GC.
     *
     *  4. Out-of-memory errors on allocation automatically trigger
     *     failure vs. needing special handling by returning NULL (which may
     *     or may not be desirable, depending on what you're doing)
     *
     * Additionally, the rebAlloc(T) and rebAllocN(T, num) macros automatically
     * cast to the correct type for C++ compatibility.
     *
     * By default, the returned memory must either be rebRepossess()'d or
     * rebFree()'d before its frame ends.  To get around this limitation,
     * you can call rebUnmanageMemory() on the pointer...but it will then no
     * longer be cleaned up automatically in case of a fail().
     */

    #define rebAlloc(T) \
        ((T*)rebAllocBytes(sizeof(T)))

    #define rebAllocN(T,n) \
        ((T*)rebAllocBytes(sizeof(T) * (n)))

    #define rebTryAlloc(T) \
        ((T*)rebTryAllocBytes(sizeof(T)))

    #define rebTryAllocN(T,n) \
        ((T*)rebTryAllocBytes(sizeof(T) * (n)))

    /* Used during boot to zero out global variables */
    static inline void rebReleaseAndNull(RebolValue** v) {
        rebRelease(*v);
        *v = rebNull;  /* nullptr may not be defined */
    }


    /*
     * !!! This is a convenience wrapper over the function that makes a
     * failure code from an OS error ID.  Since rebError_OS() links in OS
     * specific knowledge to the build, it probably doesn't belong in the
     * core build.  But to make things easier it's there for the moment.
     * Ultimately it should come from a "Windows Extension"/"POSIX extension"
     * or something otherwise.
     *
     * Note: There is no need to rebR() the handle due to the failure; the
     * handles will auto-GC.
     *
     * Because it runs rebJumpsCore() instead of rebJumps() it would not
     * pick up an overridden definition of FAIL.  It always calls LIB.FAIL,
     * which may be good (?)
     */

    #define rebFail_OS(errnum) \
        rebJumpsCore("fail", rebR(rebError_OS(errnum)));


    #endif  /* REBOL_H_1020_0304 */
}]

e-lib/write-emitted


=== "GENERATE TMP-REBOL-API-TABLE.C" ===

; The form of the API which is exported as a table is declared as a struct,
; but there has to be an instance of that struct filled with the actual
; pointers to the API_XXX C functions to be able to hand it to clients.  Only
; one instance of this table should be linked into Rebol.

e-table: make-emitter "REBOL Interface Table Singleton" (
    join prep-dir %core/tmp-rebol-api-table.c
)

table-init-items: map-each-api [
    unspaced ["&" "API_" name]
]

e-table/emit [table-init-items {
    #include "rebol.h"

    RebolApiTable g_librebol = {
        $(Table-Init-Items),
    };
}]

e-table/write-emitted


=== "GENERATE REB-CWRAPS.JS AND OTHER FILES FOR EMCC" ===

; !!! The JavaScript extension is intended to be moved to its own location
; with its own bug tracker (and be under an LGPL license instead of Apache2.)
; However, there isn't yet a viable build hook for offering the API
; construction.  But one is needed...not just for the JavaScript extension,
; but also so that the TCC extension can get a list of APIs to export the
; symbols for.
;
; For now, just DO the file at an assumed path--in a state where it can take
; for granted that the list of APIs and the `CSCAPE` emitter is available.
;
; !!! With module isolation this becomes difficult, work around it by binding
; the code into this module.

saved-dir: what-dir

; The JavaScript extension actually mutates the API table, so run the TCC hook
; first...
;
change-dir (join repo-dir %extensions/tcc/tools/)
do overbind (binding of inside [] 'prep-dir) load %prep-libr3-tcc.reb

change-dir (join repo-dir %extensions/javascript/tools/)
do overbind (binding of inside [] 'prep-dir) load %prep-libr3-js.reb

change-dir saved-dir
