REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make libRebol related files (for %rebol.h)"
    File: %make-reb-lib.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

do %common.r
do %common-parsers.r
do %common-emitter.r

print "--- Make Reb-Lib Headers ---"

args: parse-args system/script/args  ; either from command line or DO/ARGS
output-dir: make-file [(system/options/path) prep /]
output-dir: make-file [(output-dir) include /]
mkdir/deep output-dir

ver: load %../src/boot/version.r


=== PROCESS %a-lib.h TO PRODUCE DESCRIPTION OBJECTS FOR EACH API ===

; This leverages the prototype parser, which uses PARSE on C lexicals, and
; loads Rebol-structured data out of comments in the file.
;
; Currently only %a-lib.c is searched for RL_API entries.  This makes it
; easier to track the order of the API routines and change them sparingly
; (such as by adding new routines to the end of the list, so as not to break
; binary compatibility with code built to the old ordered interface).  The
; point of needing that stability hasn't been reached yet, but will come.
;
; !!! Having the C parser doesn't seem to buy us as much as it sounds, as
; this code has to parse out the types and parameter names.  Is there a way
; to hook it to get this information?

api-objects: make block! 50

map-each-api: func [code [block!]] [
    map-each api api-objects compose/only [
        do in api (code)  ; want API variable visible to `code` while running 
    ]
]

emit-proto: func [return: <void> proto] [
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

    if header/2 != 'RL_API [return]
    if not set-word? header/1 [
        fail ["API declaration should be a SET-WORD!, not" (header/1)]
    ]

    paramlist: collect [
        parse proto [
            copy returns to "RL_" "RL_" copy name to "(" skip
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
                    while [find identifier-chars pos/1] [
                        pos: back pos
                    ]
                    keep trim/tail copy/part param next pos  ; TEXT! of type
                    keep to word! next pos  ; WORD! of the parameter name
                )
            ]]
            end
        ] else [
            fail ["Couldn't extract API schema from prototype:" proto]
        ]
    ]

    if (to set-word! name) != header/1 [  ; e.g. `//  rebValue: RL_API`
        fail [
            "Name in comment header (" header/1 ") isn't C function name"
            "minus RL_ prefix to match" (name)
        ]
    ]

    if is-variadic: did find paramlist 'vaptr [
        parse paramlist [
            ;
            ; `quotes` is first to facilitate C99 macros that want two places
            ; to splice arguments: head and tail, e.g.
            ;
            ;     #define rebFoo(...) RL_rebFoo(0, __VA_ARGS__, rebEND)
            ;     #define rebFooQ(...) RL_rebFoo(1, __VA_ARGS__, rebEND)
            ;
            ; `quotes` may generalize to more `modes` or `flags` someday.
            ;
            "unsigned char" 'quotes

            copy paramlist: to "const void *"  ; signal start of variadic

            "const void *" 'p
            "va_list *" 'vaptr
        ] else [
            fail [name "has unsupported variadic paramlist:" mold paramlist]
        ]
    ]

    ; Note: Cannot set object fields directly from PARSE, tried it :-(
    ; https://github.com/rebol/rebol-issues/issues/2317
    ;
    append api-objects make object! compose/only [
        spec: try match block! third header  ; Rebol metadata API comment
        name: (ensure text! name)
        returns: (ensure text! trim/tail returns)
        paramlist: (ensure block! paramlist)
        proto: (ensure text! proto)
        is-variadic: (ensure logic! is-variadic)
    ]
]

process: func [file] [
    data: read the-file: file
    data: to-text data

    proto-parser/emit-proto: :emit-proto
    proto-parser/process data
]

src-dir: %../src/core/

process src-dir/a-lib.c


=== GENERATE LISTS USED TO BUILD REBOL.H ===

; For readability, the technique used is not to emit line-by-line, but to
; give a "big picture overview" of the header file.  It is substituted into
; like a conventional textual templating system.  So blocks are produced for
; long generated lists, and then spliced into slots in that "big picture"

extern-prototypes: map-each-api [
    cscape/with {RL_API $<Proto>} api
]

lib-struct-fields: map-each-api [
    cfunc-params: delimit ", " compose [
        (if is-variadic ["unsigned char quotes"])
        ((map-each [type var] paramlist [spaced [type var]]))
        (if is-variadic ["const void *p"])
        (if is-variadic ["va_list *vaptr"])
    ]
    cfunc-params: default ["void"]
    cscape/with {$<Returns> (*$<Name>)($<Cfunc-Params>)} api
]

non-variadics: make block! length of api-objects
c89-variadic-inlines: make block! length of api-objects
c++-variadic-inlines: make block! length of api-objects

for-each api api-objects [do in api [
    if find spec #noreturn [
        assert [returns = "void"]
        opt-dead-end: "DEAD_END;"
        opt-noreturn: "ATTRIBUTE_NO_RETURN"
    ] else [
        opt-dead-end: _
        opt-noreturn: _
    ]

    opt-return: try if returns != "void" ["return"]

    make-c-proxy: function [
        return: [text!]
        /Q
        /inline
        <with> returns wrapper-params
    ][
        q: try if q ["Q"]
        inline: try if inline ["_inline"]

        returns: default ["void"]

        wrapper-params: default ["void"]

        cscape/with {
            $<OPT-NORETURN>
            inline static $<Returns> $<Name>$<Q>$<inline>($<Wrapper-Params>) {
                $<Opt-Va-Start>
                $<opt-return> LIBREBOL_PREFIX($<Name>)($<Proxied-Args>);
                $<OPT-DEAD-END>
            }
        } compose [
            wrapper-params  ; "global" where q is undefined, must be first
            (api)
            q  ; !!! Binding to all words in contexts
        ]
    ]

    make-c++-proxy: function [
        return: [text!]
        /Q
        <with> returns wrapper-params
    ][
        q: try if q ["Q"]

        returns: default ["void"]

        wrapper-params: default ["void"]

        cscape/with {
            template <typename... Ts>
            $<OPT-NORETURN>
            inline static $<Returns> $<Name>$<Q>($<Wrapper-Params>) {
                LIBREBOL_PACK_CPP_ARGS;
                $<opt-return> LIBREBOL_PREFIX($<Name>)($<Proxied-Args>);
                $<OPT-DEAD-END>
            }
        } compose [
            wrapper-params  ; "global" where q is undefined, must be first
            (api)
            q  ; !!! Binding to all words in contexts
        ]
    ]

    if is-variadic [
        ;
        ; FIRST THE C VERSIONS
        ; These take `const void *p` and `...`

        opt-va-start: {va_list va; va_start(va, p);}

        wrapper-params: delimit ", " compose [
            ((map-each [type var] paramlist [spaced [type var]]))
            "const void *p"
            "..."
        ]

        ; We need two versions of the inline function for C89, one for Q to
        ; quote spliced slots and one normal.

        proxied-args: delimit ", " compose [
            "0" ((map-each [type var] paramlist [to-text var])) "p" "&va"
        ]
        append c89-variadic-inlines make-c-proxy/inline

        proxied-args: delimit ", " compose [
            "1" ((map-each [type var] paramlist [to-text var])) "p" "&va"
        ]
        append c89-variadic-inlines make-c-proxy/inline/Q


        ; NOW THE C++ VERSIONS
        ; these take `const Ts &... args`

        wrapper-params: delimit ", " compose [
            ((map-each [type var] paramlist [spaced [type var]]))
            "const Ts &... args"
        ]

        ; We need two versions of the inline function for C++, one for Q to
        ; quote spliced slots and one normal.

        proxied-args: delimit ", " compose [
            "0"
            ((map-each [type var] paramlist [to-text var]))
            "packed"
            "nullptr"
        ]
        append c++-variadic-inlines make-c++-proxy

        proxied-args: delimit ", " compose [
            "1"
            ((map-each [type var] paramlist [to-text var]))
            "packed"
            "nullptr"
        ]
        append c++-variadic-inlines make-c++-proxy/Q
    ]
    else [
        opt-va-start: _

        wrapper-params: try delimit ", " map-each [type var] paramlist [
            spaced [type var]
        ]

        proxied-args: try delimit ", " map-each [type var] paramlist [
            to text! var
        ]

        append non-variadics make-c-proxy
    ]
]]

c89-macros: collect [ map-each-api [
    if is-variadic [
        keep cscape/with {#define $<Name> $<Name>_inline} api
        keep cscape/with {#define $<Name>Q $<Name>Q_inline} api
    ]
] ]

c99-or-c++11-macros: collect [ map-each-api [
    ;
    ; C99/C++11 have the ability to do variadic macros, giving the power to
    ; implicitly slip a rebEND signal at the end of the parameter list.  This
    ; overcomes a C variadic function's fundamental limitation of not being
    ; able to implicitly know the number of variadic parameters used.
    ;
    ; We make two entries for each variadic splicer--one that quotes splices
    ; (e.g. rebDidQ) and one that does not (plain rebDid)
    ;
    ; !!! We could likely do better than this, e.g. if the `quotes` were moved
    ; to the first parameter, it could be passed as 0 or 1 here and use only
    ; one inline function instead of two.  But so long as C89 support is
    ; a given, it would just make the header file larger to add that variant.
    ;
    if is-variadic [
        keep cscape/with
            {#define $<Name>(...) $<Name>_inline(__VA_ARGS__, rebEND)} api
        keep cscape/with
            {#define $<Name>Q(...) $<Name>Q_inline(__VA_ARGS__, rebEND)} api
    ]
] ]


=== GENERATE REBOL.H ===

; Rather than put too many comments here in the Rebol, err on the side of
; putting comments in the header itself.  `/* use old C style comments */`
; to help cue readers to knowing they're reading generated code and don't
; edit, since the Rebol codebase at large uses `//`-style comments.

e-lib: (make-emitter
    "Rebol External Library Interface" output-dir/rebol.h)

e-lib/emit {
    #ifndef REBOL_H_1020_0304  /* "include guard" allows multiple #includes */
    #define REBOL_H_1020_0304  /* numbers in case REBOL_H defined elsewhere */

    /*
     * Some features are enhanced by the presence of a C++11 compiler or
     * above.  This ranges from simple things like the ability to detect a
     * missing `rebEND` when REBOL_EXPLICIT_END is in effect, to allowing
     * `int` or `std::string` parameters to APIs instead of using `rebI()` or
     * `rebT()`.  These are on by default if the compiler is capable, but
     * can be suppressed.
     *
     * (Note that there's still some conditional code based on __cplusplus
     * even when this switch is used, but not in a way that affects the
     * runtime behavior uniquely beyond what C99 would do.)
     */
    #if !defined(REBOL_NO_CPLUSPLUS)
        #if defined(__cplusplus) && __cplusplus >= 201103L
            /* C++11 or above, if following the standard (VS2017 does not) */
        #elif defined (CPLUSPLUS_11)
            /* Custom C++11 or above flag, to override Visual Studio's lie */
        #else
            #define REBOL_NO_CPLUSPLUS  /* compiler not current enough */
        #endif
    #endif

    /*
     * The goal is to make it possible that the only include file one needs
     * to make a simple Rebol library client is `#include "rebol.h"`.  Yet
     * pre-C99 or pre-C++11 compilers will need `#define REBOL_EXPLICIT_END`
     * since variadic macros don't work.  They will also need shims for
     * stdint.h and stdbool.h included.
     */
    #include <stdlib.h>  /* for size_t */
    #include <stdarg.h>  /* for va_list, va_start() in inline functions */
    #if !defined(_PSTDINT_H_INCLUDED) && !defined(LIBREBOL_NO_STDINT)
        #include <stdint.h>  /* for uintptr_t, int64_t, etc. */
    #endif
    #if !defined(_PSTDBOOL_H_INCLUDED) && !defined(LIBREBOL_NO_STDBOOL)
        #if !defined(__cplusplus)
            #include <stdbool.h>  /* for bool, true, false (if C99) */
        #endif
    #endif

    /*
     * !!! Needed by following two macros.
     */
    #ifndef __has_builtin
        #define __has_builtin(x) 0
    #endif
    #if !defined(GCC_VERSION_AT_LEAST)  /* !!! duplicated in %reb-config.h */
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
     */
    #if !defined(ATTRIBUTE_NO_RETURN)  /* !!! duplicated in %reb-config.h */
        #if defined(__clang__) || GCC_VERSION_AT_LEAST(2, 5)
            #define ATTRIBUTE_NO_RETURN __attribute__ ((noreturn))
        #elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
            #define ATTRIBUTE_NO_RETURN _Noreturn
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
            __declspec(noreturn) static inline void msvc_unreachable(void) {
                while (1) { }
            }
            #define DEAD_END msvc_unreachable()
        #else
            #define DEAD_END
        #endif
    #endif

    /*
     * !!! These constants are part of an old R3-Alpha versioning system
     * that hasn't been paid much attention to.  Keeping as a placeholder.
     */
    #define RL_VER $<ver/1>
    #define RL_REV $<ver/2>
    #define RL_UPD $<ver/3>

    /*
     * The API can be used by the core on value cell pointers that are in
     * stable locations guarded by GC (e.g. frame argument or output cells).
     * Since the core uses REBVAL*, it must be accurate (not just a void*)
     */
    struct Reb_Value;
    #ifdef __cplusplus
        #define REBVAL Reb_Value  /* `struct` breaks MS variadic templates */
    #else
        #define REBVAL struct Reb_Value
    #endif

    /*
     * "Instructions" in the API are not REBVAL*, and you are not supposed
     * to cache references to them (e.g. in variables).  They are only for
     * use in the variadic calls, because the feeding of the va_list in
     * case of error is the only way they are cleaned up.
     */
    struct Reb_Node;
    #ifdef __cplusplus
        #define REBINS Reb_Node  /* `struct` breaks MS variadic templates */
    #else
        #define REBINS struct Reb_Node
    #endif

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
     * REBVAL* but does not have to be.  Result must be a REBVAL* or NULL.
     *
     * !!! If the dangerous function returns an ERROR!, it will currently be
     * converted to null, which parallels TRAP without a handler.  nulls will
     * be converted to voids.
     */
    typedef REBVAL* (REBDNG)(void *opaque);

    /*
     * "Rescue Function" called as the handler in rebRescueWith().  Receives
     * the REBVAL* of the error that occurred, and the opaque pointer.
     *
     * !!! If either the dangerous function or the rescuing function return an
     * ERROR! value, that is not interfered with the way rebRescue() does.
     */
    typedef REBVAL* (REBRSC)(REBVAL *error, void *opaque);

    /*
     * For some HANDLE!s GC callback
     */
    typedef void (CLEANUP_CFUNC)(const REBVAL*);

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
        ((REBVAL*)0)

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
     *
     * Note: We use a `void*` for this because it needs to be suitable for
     * the same alignment as character.  The C++ build checks that void*
     * are only allowed in the last position.  This isn't foolproof, but it's
     * better than breaking the standard (e.g. by casting to be a pointer to
     * something with another alignment).
     */
    #define rebEND \
        ((const void*)"\x80")

    /*
     * SHORTHAND MACROS
     *
     * These shorthand macros make the API somewhat more readable, but as
     * they are macros you can redefine them to other definitions if you want.
     *
     * THESE DON'T WORK IN JAVASCRIPT, so when updating them be sure to update
     * the JavaScript versions, which have to make ordinary stub functions.
     * (The C portion of the Emscripten build can use these internally, as
     * the implementation is C.  But when calling the lib from JS, it is
     * obviously not reading this generated header file!)
     */

    #define rebR rebRELEASING

    #define rebT(utf8) \
        rebR(rebText(utf8))  /* might rebTEXT() delayed-load? */

    #define rebI(int64) \
        rebR(rebInteger(int64))

    #define rebL(flag) \
        rebR(rebLogic(flag))

    #ifdef REBOL_EXPLICIT_END
        /*
         * Most invocations of rebQ() are single-arity.  Avoid common pain
         * in the C89 builds by making the shorthand work (if truly needed
         * to quote multiple items, rebQUOTING/rebUNQUOTING are available).
         */
        #define rebQ(v) \
            rebQUOTING((v), rebEND)  /* 1-arg + end optimized in rebQ() */

        #define rebU(v) \
            rebUNQUOTING((v), rebEND)  /* 1-arg + end optimized in rebU() */
    #else
        #define rebQ rebQUOTING
        #define rebU rebUNQUOTING
    #endif


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
     * !!! Should this use LIB/FAIL instead of FAIL?
     */
    #define rebFail_OS(errnum) \
        rebJumps("fail", rebR(rebError_OS(errnum)), rebEND);

    #ifdef __cplusplus
    extern "C" {
    #endif

    /*
     * Function entry points for reb-lib.  Formulating this way allows the
     * interface structure to be passed from an EXE to a DLL, then the DLL
     * can call into the EXE (which is not generically possible via linking).
     *
     * For convenience, calls to RL->xxx are wrapped in inline functions:
     */
    typedef struct rebol_ext_api {
        $[Lib-Struct-Fields];
    } RL_LIB;

    #ifdef REB_EXT /* can't direct call into EXE, must go through interface */
        /*
         * The inline functions below will require this base pointer:
         */
        extern RL_LIB *RL; /* is passed to the RX_Init() function */

        #define LIBREBOL_PREFIX(api_name) RL->##api_name

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

        #define LIBREBOL_PREFIX(api_name) RL_##api_name

        /*
         * Extern prototypes for RL_XXX, don't call these functions directly.
         * They use vaptr instead of `...`, and may not do all the proper
         * exception/longjmp handling needed.
         */

        $[Extern-Prototypes];

    #endif  /* !REB_EXT */

    /*
     * API functions that are not variadic (no complex wrapping)
     */

    $[Non-Variadics]

    #ifdef __cplusplus
    }  /* end the extern "C" */
    #endif

    #if defined(REBOL_NO_CPLUSPLUS)
        /*
         * Plain C only has va_list as a method of taking variable arguments.
         */

        $[C89-Variadic-Inlines]

        /*
         * C's variadic interface is low-level, as a thin wrapper over the
         * stack memory of a function call.  So va_start() and va_end() aren't
         * usually function calls...in fact, va_end() is usually a no-op.
         *
         * The simplicity is an advantage for optimization, but unsafe!  Type
         * checking is non-existent, and there is no protocol for knowing how
         * many items are in a va_list.  The libRebol API uses rebEND to
         * signal termination, but it is awkward and easy to forget.
         *
         * C89 offers no real help, but C99 (and C++11 onward) standardize an
         * interface for variadic macros:
         *
         * https://stackoverflow.com/questions/4786649/
         *
         * These macros can transform variadic input in such a way that a
         * rebEND may be automatically placed on the tail of a call.  If
         * rebEND is used explicitly, this gives a harmless but slightly i
         * efficient repetition.
         */
        #if !defined(REBOL_EXPLICIT_END)
          /*
           * Clients who #include "rebol.h" aren't expected to use %sys-core.h
           * internal APIs (including %sys-core.h hs %rebol.h automatically).
           * But for some debugging scenarios, adding the internal API as an
           * "overlay" gives extra functions for recompiling test code which
           * does things like pick apart cell structure.  If that is the case,
           * then this allows %sys-core.h to detect that %rebol.h was already
           * included and is using explicit ends (the core uses explicit ones,
           * but that is harmlessly redundant for a debug build...each call
           * will just have two rebENDs--one explicit, one implicit).
           */
          #define REBOL_IMPLICIT_END

          #if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
            /* C99 or above */
          #elif defined(__cplusplus) && __cplusplus >= 201103L
            /* C++11 or above, if following the standard (VS2017 does not) */
          #elif defined (CPLUSPLUS_11)
            /* Custom C++11 or above flag, to override Visual Studio's lie */
          #else
            #error "REBOL_EXPLICIT_END must be used prior to C99 or C+++11"
          #endif

            $[C99-Or-C++11-Macros]

        #else  /* REBOL_EXPLICIT_END */

            /*
             * !!! Some kind of C++ variadic trick using template recursion could
             * check to make sure you used a rebEND under this interface, when
             * building the C89-targeting code under C++11 and beyond.  TBD.
             */

            $[C89-Macros]

        #endif  /* REBOL_EXPLICIT_END */


        /*
         * The NOMACRO version of the API is one in which you can use macros
         * inside the call.  If you're using C this means you'll have to
         * explicitly put a rebEND on, whether using C99 or not.
         */
        #define LIBREBOL_NOMACRO(api) api##_inline
    #else
        /*
         * If using C++, variadic calls can be type-checked to make sure only
         * legal arguments are passed.  It also means one can pass literals
         * and have them coerced (e.g. integer => INTEGER! or bool => LOGIC!).
         *
         * Note: In MSVC, `#include <string>` will pull in `<xstring>` that
         * then sucks in `<iosfwd>` which brings in `<cstdio>`.  This means
         * that including the release version of %sys-core.h will see an
         * inclusion of stdio that it doesn't want.  Bypass the assertion
         * for this case, and hope the C build maintains dependency purity.
         */
        #ifdef TO_WINDOWS
            #define REBOL_ALLOW_STDIO_IN_RELEASE_BUILD  // ^^-- see above
        #endif
        #include <string>
        #include <type_traits>

        inline static const void *to_rebarg(const REBVAL *val)
            { return val; }

        inline static const void *to_rebarg(const REBINS *ins)
            { return ins; }

        inline static const void *to_rebarg(const char *source)
            { return source; }  /* not TEXT!, but LOADable source code */

        inline static const void *to_rebarg(bool b)
            { return rebL(b); }

        inline static const void *to_rebarg(int i)
            { return rebI(i); }

        inline static const void *to_rebarg(double d)
            { return rebR(rebDecimal(d)); }

        inline static const void *to_rebarg(const std::string &text)
          { return rebT(text.c_str()); }  /* std::string acts as TEXT! */

        inline static const void *to_rebarg(const void *end)
          { return end; }

        /*
         * Parameters are packed into an array whose size is known at
         * compile-time, into a `std::array` onto the stack.  This yields
         * something like a va_list, and the API is able to treat the `p`
         * first parameter as a packed array of this kind if vaptr is nullptr.
         *
         * The packing is done by a recursive process, and the terminal
         * state of the recursion can check for whether there's a rebEND there
         * or not.
         */
        #ifdef REBOL_EXPLICIT_END
            template <typename Last>
            void rebArgRecurser_internal(
                int i,
                const void* data[],
                const Last &last
            ){
                static_assert(
                    std::is_same<const void*, Last>::value,
                    "REBOL_EXPLICIT_END means rebEND must be last argument"
                );
                data[i] = last;  /* hopefully rebEND (test it?) */
            }

            #define LIBREBOL_PACK_CPP_ARGS \
                const size_t num_args = sizeof...(args); \
                const void* packed[num_args]; \
                rebArgRecurser_internal(0, packed, args...);
        #else
            template <typename Last>
            void rebArgRecurser_internal(
                int i,
                const void* data[],
                const Last &last
            ){
                data[i] = to_rebarg(last);
            }

            /* full specialization alternative for last item */
            /*
             * Note: it may seem useful to prohibit rebEND in the cases
             * where it is implicit, but this inhibits the sharing of
             * inline code intended to be used with either.  Review.
             *
             * !!! This isn't working, so `to_rebarg` allows `const void*`
             * which is not ideal.  Remove that when this is made to work.
             */
            /*template<>
            void rebArgRecurser_internal<void*>(
                int i,
                const void* data[],
                const void* &last
            ){
                data[i] = last;
            }*/

            #define LIBREBOL_PACK_CPP_ARGS \
                const size_t num_args = sizeof...(args); \
                const void* packed[num_args + 1]; \
                rebArgRecurser_internal(0, packed, args...); \
                packed[num_args] = rebEND;
        #endif

        template <typename First, typename... Rest>
        void rebArgRecurser_internal(
            int i,
            const void* data[],
            const First &first, const Rest &... rest
        ){
            data[i] = to_rebarg(first);
            rebArgRecurser_internal(i + 1, data, rest...);
        }


        /*
         * C++ Entry Points
         */

        $[C++-Variadic-Inlines]

        /*
         * The NOMACRO version of the API is one in which you can use macros
         * inside the call.  If you're using C99 this means you'll have to
         * explicitly put a rebEND on.  No special action w/C++ wrappers.
         */
        #define LIBREBOL_NOMACRO(api) api
    #endif  /* C++ versions */

    /*
     * TYPE-SAFE rebMalloc() MACRO VARIANTS
     *
     * rebMalloc() offers some advantages over hosts just using malloc():
     *
     *  1. Memory can be retaken to act as a BINARY! series without another
     *     allocation, via rebRepossess().
     *
     *  2. Memory is freed automatically in the case of a failure in the
     *     frame where the rebMalloc() occured.  This is especially useful
     *     when mixing C code involving allocations with rebValue(), etc.
     *
     *  3. Memory gets counted in Rebol's knowledge of how much memory the
     *     system is using, for the purposes of triggering GC.
     *
     *  4. Out-of-memory errors on allocation automatically trigger
     *     failure vs. needing special handling by returning NULL (which may
     *     or may not be desirable, depending on what you're doing)
     *
     * Additionally, the rebAlloc(type) and rebAllocN(type, num) macros
     * automatically cast to the correct type for C++ compatibility.
     *
     * Note: There currently is no rebUnmanage() equivalent for rebMalloc()
     * data, so it must either be rebRepossess()'d or rebFree()'d before its
     * frame ends.  This limitation will be addressed in the future.
     */

    #define rebAlloc(t) \
        ((t*)rebMalloc(sizeof(t)))
    #define rebAllocN(t,n) \
        ((t*)rebMalloc(sizeof(t) * (n)))


    #endif  /* REBOL_H_1020_0304 */
}

e-lib/write-emitted


=== GENERATE TMP-REB-LIB-TABLE.INC ===

; The form of the API which is exported as a table is declared as a struct,
; but there has to be an instance of that struct filled with the actual
; pointers to the RL_XXX C functions to be able to hand it to clients.  Only
; one instance of this table should be linked into Rebol.

e-table: (make-emitter
    "REBOL Interface Table Singleton" output-dir/tmp-reb-lib-table.inc)

table-init-items: map-each-api [
    unspaced ["RL_" name]
]

e-table/emit {
    RL_LIB Ext_Lib = {
        $(Table-Init-Items),
    };
}

e-table/write-emitted


=== GENERATE REB-CWRAPS.JS AND OTHER FILES FOR EMCC ===

; !!! The JavaScript extension is intended to be moved to its own location
; with its own bug tracker (and be under an LGPL license instead of Apache2.)
; However, there isn't yet a viable build hook for offering the API
; construction.  But one is needed...not just for the JavaScript extension,
; but also so that the TCC extension can get a list of APIs to export the
; symbols for.
;
; For now, just DO the file at an assumed path--in a state where it can take
; for granted that the list of APIs and the `CSCAPE` emitter is available.

; The JavaScript extension actually mutates the API table, so run the TCC hook
; first...
;
do repo/tools/../extensions/tcc/prep-libr3-tcc.reb

do repo/tools/../extensions/javascript/prep-libr3-js.reb
