REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make libRebol related files (for %rebol.h)"
    File: %make-librebol.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

; **SENSITIVE MAGIC LINE OF VOODOO** - see "Usage" in %bootstrap-shim.r
(change-dir do join copy system/script/path %bootstrap-shim.r)

do <common.r>
do <common-parsers.r>
do <common-emitter.r>

print "--- Make Reb-Lib Headers ---"

args: parse-args system/options/args
output-dir: system/options/path/prep
output-dir: output-dir/include
mkdir/deep output-dir

ver: load <../src/boot/version.r>


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; PROCESS %a-lib.h TO PRODUCE A LIST OF DESCRIPTION OBJECTS FOR EACH API
;;
;; This leverages the prototype parser, which uses PARSE on C lexicals, and
;; loads Rebol-structured data out of comments in the file.
;;
;; Currently only one file (%a-lib.c) is searched for API entries.  So it is
;; easier to track the order of the API routines and change them sparingly
;; (such as by adding new routines to the end of the list, so as not to break
;; binary compatibility with code built to the old ordered interface).
;;
;; !!! Having the C parser doesn't seem to buy us as much as it sounds, as
;; this code has to parse out the types and parameter names.  Is there a way
;; to hook it to get this information?
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

api-objects: make block! 50

map-each-api: func [code [block!]] [
    map-each api api-objects compose/only [
        do in api (code) ;-- want API variable available when code is running
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

    if header/2 != 'API [return]
    if not set-word? header/1 [
        fail ["API declaration should be a SET-WORD!, not" (header/1)]
    ]

    paramlist: collect [
        parse2/match proto [
            copy returns to "API_" "API_" copy name to "(" skip
            ["void)" | some [ ;-- C void, or at least one parameter expected
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
                        ;-- #"." in variadics (but all va_list* in API defs)
                    ]
                    pos: back tail param
                    while [find identifier-chars pos/1] [
                        pos: back pos
                    ]
                    keep trim/tail copy/part param next pos ;-- TEXT! of type
                    keep to word! next pos ;-- WORD! of the parameter name
                )
            ]]
        ] else [
            fail ["Couldn't extract API schema from prototype:" proto]
        ]
    ]

    if (to set-word! name) != header/1 [ ;-- e.g. `//  rebValue: API`
        fail [
            "Name in comment header (" header/1 ") isn't C function name"
            "minus API_ prefix to match" (name)
        ]
    ]

    ; Note: Cannot set object fields directly from PARSE, tried it :-(
    ; https://github.com/rebol/rebol-issues/issues/2317
    ;
    append api-objects make object! compose/only [
        spec: null-to-blank match block! third header ;-- metadata API comment
        name: (ensure text! name)
        returns: (ensure text! trim/tail returns)
        paramlist: (ensure block! paramlist)
        proto: (ensure text! proto)
    ]
]

process: func [file] [
    data: read the-file: file
    data: to-text data

    proto-parser/emit-proto: :emit-proto
    proto-parser/process data
]

src-dir: clean-path append copy repo-dir %src/core/

process src-dir/a-lib.c


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; GENERATE LISTS USED TO BUILD REBOL.H
;;
;; For readability, the technique used is not to emit line-by-line, but to
;; give a "big picture overview" of the header file.  It is substituted into
;; like a conventional textual templating system.  So blocks are produced for
;; long generated lists, and then spliced into slots in that "big picture"
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

extern-prototypes: map-each-api [
    cscape/with
        <- {$<Proto>}
        <- api
]

direct-call-inlines: make block! length of api-objects

for-each api api-objects [do in api [
    if find [
        "rebEnterApi_internal" ; called as API_rebEnterApi_internal
    ] name [
        continue
    ]

    opt-va-start: _
    if va-pos: find paramlist "va_list *" [
        assert ['vaptr first next va-pos]
        assert ['p = first back va-pos]
        assert ["const void *" = first back back va-pos]
        opt-va-start: {va_list va; va_start(va, p);}
    ]

    wrapper-params: (delimit ", " map-each [type var] paramlist [
        if type = "va_list *" [
            "..."
        ] else [
            spaced [type var]
        ]
    ]) else ["void"]

    proxied-args: null-to-blank delimit ", " map-each [type var] paramlist [
        if type = "va_list *" [
            "&va" ;-- to produce vaptr
        ] else [
            to text! var
        ]
    ]

    if find maybe+ spec #noreturn [
        assert [returns = "void"]
        opt-dead-end: "DEAD_END;"
        opt-noreturn: "ATTRIBUTE_NO_RETURN"
    ] else [
        opt-dead-end: _
        opt-noreturn: _
    ]

    opt-return: null-to-blank if returns != "void" ["return"]

    enter: null-to-blank if name != "rebStartup" [
        copy "API_rebEnterApi_internal();^/"
    ]

    make-inline-proxy: func [
        return: [text!]
        internal [text!]
    ][
        cscape/with {
            $<OPT-NORETURN>
            inline static $<Returns> $<Name>_inline($<Wrapper-Params>) {
                $<Enter>
                $<Opt-Va-Start>
                $<opt-return> $<Internal>($<Proxied-Args>);
                $<OPT-DEAD-END>
            }
        } reduce [api 'internal]
    ]

    append direct-call-inlines make-inline-proxy unspaced ["API_" name]
]]

c99-or-c++11-macros: map-each-api [
    if find paramlist 'vaptr [
        cscape/with
            <- {#define $<Name>(...) $<Name>_inline(__VA_ARGS__, rebEND)}
            <- api
    ] else [
        cscape/with
            <- {#define $<Name> $<Name>_inline}
            <- api
    ]
]


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; GENERATE REBOL.H
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

e-lib: (make-emitter
    "Rebol External Library Interface" output-dir/rebol.h)

e-lib/emit {
    /*
     * The goal is to make it possible that the only include file one needs
     * to make a simple Rebol library client is `#include "rebol.h"`.
     */
    #include <stdlib.h> /* for size_t */
    #include <stdarg.h> /* for va_list, va_start() in inline functions */
    #if !defined(_PSTDINT_H_INCLUDED) && !defined(REBOL_NO_STDINT)
        #include <stdint.h> /* for uintptr_t, int64_t, etc. */
    #endif
    #if !defined(_PSTDBOOL_H_INCLUDED) && !defined(REBOL_NO_STDBOOL)
        #if !defined(__cplusplus)
            #include <stdbool.h> /* for bool, true, false (if C99) */
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
     *
     * We use `inline static` here in the C function vs plain `inline` due to
     * pragmatic issues.  See comments on the INLINE macro in %c-enhanced.h for
     * why C builds of the core define INLINE as `static inline` in C builds
     * and `inline` in C++ builds.  That macro is avoided in this header to
     * avoid potential conflicts with INLINE definitions in the utilizing code.
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


    #ifdef __cplusplus
    extern "C" {
    #endif

    /*
     * !!! These constants are part of an old R3-Alpha versioning system
     * that hasn't been paid much attention to.  Keeping as a placeholder.
     */
    #define LIBREBOL_VERSION $<ver/1>
    #define LIBREBOL_MAJOR $<ver/2>
    #define LIBREBOL_MINOR $<ver/3>

    /*
     * The API can be used by the core on value cell pointers that are in
     * stable locations guarded by GC (e.g. frame argument or output cells).
     * Since the core uses Value*, it must be accurate (not just a void*)
     */
    struct Reb_Value;
    typedef struct Reb_Value RebolValue;

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
    #ifdef TO_WINDOWS
        #define REBWCHAR wchar_t
    #else
        #define REBWCHAR uint16_t
    #endif

    /*
     * "Dangerous Function" which is called by rebRescue().  Argument can be a
     * Value* but does not have to be.  Result must be a Value* or nullptr.
     *
     * !!! If the dangerous function returns an ERROR!, it will currently be
     * converted to null, which parallels TRAP without a handler.  nulls will
     * be converted to voids.
     */
    typedef RebolValue* (REBDNG)(void* opaque);

    /*
     * "Rescue Function" called as the handler in rebRescueWith().  Receives
     * the Value* of the error that occurred, and the opaque pointer.
     *
     * !!! If either the dangerous function or the rescuing function return an
     * ERROR! value, that is not interfered with the way rebRescue() does.
     */
    typedef RebolValue* (REBRSC)(RebolValue* error, void* opaque);

    /*
     * For some HANDLE!s GC callback
     */
    typedef void (CLEANUP_CFUNC)(const RebolValue*);

    /*
     * The API maps Rebol's `null` to C's 0 pointer, **but don't use NULL**.
     * Some C compilers define NULL as simply the constant 0, which breaks
     * use with variadic APIs...since they will interpret it as an integer
     * and not a pointer.
     *
     * **It's best to use C++'s `nullptr`**, or a suitable C shim for it,
     * e.g. `#define nullptr ((void*)0)`.  That helps avoid obscuring the
     * fact that the Rebol API's null really is C's null, and is conditionally
     * false.  Seeing `rebNull` in source doesn't as clearly suggest this.
     *
     * However, **using NULL is broken, so don't use it**.  This macro is
     * provided in case defining `nullptr` is not an option--for some reason.
     */
    #define rebNull \
        ((RebolValue*)0)

    /*
     * Since a C nullptr (pointer cast of 0) is used to represent the Rebol
     * `null` in the API, something different must be used to indicate the
     * end of variadic input.  So a pointer to data is used where the first
     * byte is illegal for starting UTF-8 (a continuation byte, first bit 1,
     * second bit 0) and the second byte is 0.
     *
     * To Rebol, the first bit being 1 means it's a Rebol node, the second
     * that it is not in the "free" state.  The lowest bit in the first byte
     * clear indicates it doesn't point to a "cell".  The SECOND_BYTE() is
     * where the VAL_TYPE() of a cell is usually stored, and this being 0
     * indicates an END marker.
     */
    #define rebEND \
        ((const void*)"\x80")

    /*
     * Extern prototypes for API_XXX, don't call these functions directly.
     * They use vaptr instead of `...`, and may not do all the proper
     * exception/longjmp handling needed.
     */

    $[Extern-Prototypes];

    /*
     * rebXXX_inline functions which do the work of passing the variadics.
    */

    $[Direct-Call-Inlines]

    /*
     * C's variadic interface is very low-level, as a thin wrapper over the
     * stack memory of a function call.  So va_start() and va_end() aren't
     * really function calls...in fact, va_end() is usually a no-op.
     *
     * The simplicity is an advantage for optimization, but unsafe!  Type
     * checking is non-existent, and there is no protocol for knowing how
     * many items are in a va_list.  The libRebol API uses rebEND to signal
     * termination.
     *
     * C99 (and C++11 onward) standardize an interface for variadic macros:
     *
     * https://stackoverflow.com/questions/4786649/
     *
     * These macros can transform variadic input in such a way that a rebEND
     * may be automatically placed on the tail of a call.
     */
    $[C99-Or-C++11-Macros]


    /***********************************************************************
     *
     *  TYPE-SAFE rebMalloc() MACRO VARIANTS FOR C++ COMPATIBILITY
     *
     * Originally R3-Alpha's hostkit had special OS_ALLOC and OS_FREE hooks,
     * to facilitate the core to free memory blocks allocated by the host
     * (or vice-versa).  So they agreed on an allocator.  In Ren-C, all
     * layers use Value* for the purpose of exchanging such information--so
     * this purpose is obsolete.
     *
     * Yet a new API construct called rebMalloc() offers some advantages over
     * hosts just using malloc():
     *
     *     Memory can be retaken to act as a BINARY! series without another
     *     allocation, via rebRepossess().
     *
     *     Memory is freed automatically in the case of a failure in the
     *     frame where the rebMalloc() occured.  This is especially useful
     *     when mixing C code involving allocations with rebValue(), etc.
     *
     *     Memory gets counted in Rebol's knowledge of how much memory the
     *     system is using, for the purposes of triggering GC.
     *
     *     Out-of-memory errors on allocation automatically trigger
     *     failure vs. needing special handling by returning nullptr (which may
     *     or may not be desirable, depending on what you're doing)
     *
     * Additionally, the rebAlloc(type) and rebAllocN(type, num) macros
     * automatically cast to the correct type for C++ compatibility.
     *
     * Note: There currently is no rebUnmanage() equivalent for rebMalloc()
     * data, so it must either be rebRepossess()'d or rebFree()'d before its
     * frame ends.  This limitation will be addressed in the future.
     *
     **********************************************************************/

    #define rebAlloc(t) \
        cast(t *, rebMalloc(sizeof(t)))
    #define rebAllocN(t,n) \
        cast(t *, rebMalloc(sizeof(t) * (n)))


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
     */

    #define rebFail_OS(errnum) \
        rebJumps("fail", rebR(rebError_OS(errnum)));


    #ifdef __cplusplus
    }
    #endif
}

e-lib/write-emitted
