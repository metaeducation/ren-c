REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make libRebol related files (for %rebol.h)"
    File: %make-reb-lib.r
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

do %bootstrap-shim.r
do %common.r
do %common-parsers.r
do %common-emitter.r

print "--- Make Reb-Lib Headers ---"

args: parse-args system/options/args
output-dir: system/options/path/prep
output-dir: output-dir/include
mkdir/deep output-dir

ver: load %../../src/boot/version.r


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; PROCESS %a-lib.h TO PRODUCE A LIST OF DESCRIPTION OBJECTS FOR EACH API
;;
;; This leverages the prototype parser, which uses PARSE on C lexicals, and
;; loads Rebol-structured data out of comments in the file.
;;
;; Currently only two files are searched for RL_API entries.  This makes it
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

emit-proto: func [return: <void> proto] [
    header: proto-parser/data

    all [
        block? header
        2 <= length of header
        set-word? header/1
    ] or [
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
            end
        ] or [
            fail ["Couldn't extract API schema from prototype:" proto]
        ]
    ]

    if (to set-word! name) != header/1 [ ;-- e.g. `//  rebValue: RL_API`
        fail [
            "Name in comment header (" header/1 ") isn't C function name"
            "minus RL_ prefix to match" (name)
        ]
    ]

    ; Note: Cannot set object fields directly from PARSE, tried it :-(
    ; https://github.com/rebol/rebol-issues/issues/2317
    ;
    append api-objects make object! compose/only [
        spec: try match block! third header ;-- Rebol metadata API comment
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

src-dir: %../../src/core/

process src-dir/a-lib.c
process src-dir/f-extension.c ; !!! is there a reason to process this file?


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
        <- {EMSCRIPTEN_KEEPALIVE RL_API $<Proto>}
        <- api
]

lib-struct-fields: map-each-api [
    cfunc-params: if empty? paramlist [
        "void"
    ] else [
        delimit ", " map-each [type var] paramlist [
            spaced [type var]
        ]
    ]
    cscape/with
        <- {$<Returns> (*$<Name>)($<Cfunc-Params>)}
        <- api
]

struct-call-inlines: make block! length of api-objects
direct-call-inlines: make block! length of api-objects

for-each api api-objects [do in api [
    if find [
        "rebEnterApi_internal" ; called as RL_rebEnterApi_internal
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

    proxied-args: try delimit ", " map-each [type var] paramlist [
        if type = "va_list *" [
            "&va" ;-- to produce vaptr
        ] else [
            to text! var
        ]
    ]

    if find spec #noreturn [
        assert [returns = "void"]
        opt-dead-end: "DEAD_END;"
        opt-noreturn: "ATTRIBUTE_NO_RETURN"
    ] else [
        opt-dead-end: _
        opt-noreturn: _
    ]

    opt-return: try if returns != "void" ["return"]

    enter: try if name != "rebStartup" [
        copy "RL_rebEnterApi_internal();^/"
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

    append direct-call-inlines make-inline-proxy unspaced ["RL_" name]
    append struct-call-inlines make-inline-proxy unspaced ["RL->" name]
]]

c89-macros: map-each-api [
    cfunc-params: if empty? paramlist [
        "void"
    ] else [
        delimit ", " map-each [type var] paramlist [
            spaced [type var]
        ]
    ]
    cscape/with
        <- {#define $<Name> $<Name>_inline}
        <- api
]

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
     * to make a simple Rebol library client is `#include "rebol.h"`.  Yet
     * pre-C99 or pre-C++11 compilers will need `#define REBOL_EXPLICIT_END`
     * since variadic macros don't work.  They will also need shims for
     * stdint.h and stdbool.h included.
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

    #ifdef TO_EMSCRIPTEN
        /*
         * EMSCRIPTEN_KEEPALIVE is a macro in emscripten.h used to export
         * a function.  We can't include emscripten.h here (it is incompatible
         * with DONT_INCLUDE_STDIO_H)
         */
        #define EMSCRIPTEN_KEEPALIVE __attribute__((used))
    #else
        #define EMSCRIPTEN_KEEPALIVE
    #endif

    #ifdef __cplusplus
    extern "C" {
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
    #define REBVAL struct Reb_Value

    /*
     * `wchar_t` is a pre-Unicode abstraction, whose size varies per-platform
     * and should be avoided where possible.  But Win32 standardizes it to
     * 2 bytes in size for UTF-16, and uses it pervasively.  So libRebol
     * currently offers APIs (e.g. rebTextW() instead of rebText()) which
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
     */
    #define rebEND \
        ((const void*)"\x80")

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

        /*
         * Inlines to access reb-lib functions (from non-linked extensions):
         */

        $[Struct-Call-Inlines]

    #else /* ...calling Rebol as DLL, or code built into the EXE itself */
        /*
         * Extern prototypes for RL_XXX, don't call these functions directly.
         * They use vaptr instead of `...`, and may not do all the proper
         * exception/longjmp handling needed.
         */

        $[Extern-Prototypes];

        /*
         * rebXXX_inline functions which do the work of 
         */

        $[Direct-Call-Inlines]

    #endif /* !REB_EXT */

    /*
     * C's variadic interface is very low-level, as a thin wrapper over the
     * stack memory of a function call.  So va_start() and va_end() aren't
     * really function calls...in fact, va_end() is usually a no-op.
     *
     * The simplicity is an advantage for optimization, but unsafe!  Type
     * checking is non-existent, and there is no protocol for knowing how
     * many items are in a va_list.  The libRebol API uses rebEND to signal
     * termination, but it is awkward and easy to forget.
     *
     * C89 offers no real help, but C99 (and C++11 onward) standardize an
     * interface for variadic macros:
     *
     * https://stackoverflow.com/questions/4786649/
     *
     * These macros can transform variadic input in such a way that a rebEND
     * may be automatically placed on the tail of a call.  If rebEND is used
     * explicitly, this gives a harmless but slightly inefficient repetition.
     */
    #if !defined(REBOL_EXPLICIT_END)

      #if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
        /* C99 or above */
      #elif defined(__cplusplus) && __cplusplus >= 201103L
        /* C++11 or above, if following the standard (VS2017 does not) */
      #elif defined (CPLUSPLUS_11)
        /* Custom C++11 or above flag, e.g. to override Visual Studio's lie */
      #else
        #error "REBOL_EXPLICIT_END must be used prior to C99 or C+++11"
      #endif

        $[C99-Or-C++11-Macros]

    #else /* REBOL_EXPLICIT_END */

        /*
         * !!! Some kind of C++ variadic trick using template recursion could
         * check to make sure you used a rebEND under this interface, when
         * building the C89-targeting code under C++11 and beyond.  TBD.
         */

        $[C89-Macros]

    #endif /* REBOL_EXPLICIT_END */


    /***********************************************************************
     *
     *  TYPE-SAFE rebMalloc() MACRO VARIANTS FOR C++ COMPATIBILITY
     *
     * Originally R3-Alpha's hostkit had special OS_ALLOC and OS_FREE hooks,
     * to facilitate the core to free memory blocks allocated by the host
     * (or vice-versa).  So they agreed on an allocator.  In Ren-C, all
     * layers use REBVAL* for the purpose of exchanging such information--so
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
     *     failure vs. needing special handling by returning NULL (which may
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

    #ifdef __cplusplus
    }
    #endif
}

e-lib/write-emitted


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; GENERATE TMP-REB-LIB-TABLE.INC
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

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


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; GENERATE REB-LIB.JS
;;
;; !!! What should this file be called?  rebol.js isn't a good fit.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

e-cwrap: (make-emitter
    "C-Wraps" output-dir/reb-lib.js
)

to-js-type: func [
    return: [<opt> text! tag!]
    s [text!] "C type as string"
][
    case [
        s = "va_list *" [<va_ptr>] ;-- special processing, only an argument

        s = "intptr_t" [<promise>] ;-- distinct handling for return vs. arg

        ; APIs dealing with `char *` means UTF-8 bytes.  While C must memory
        ; manage such strings (at the moment), the JavaScript wrapping assumes
        ; input parameters should be JS strings that are turned into temp
        ; UTF-8 on the emscripten heap (freed after the call).  Returned
        ; `char *` should be turned into JS GC'd strings, then freed.
        ;
        ; !!! These APIs can also return nulls.  rebSpell("second [{a}]") is
        ; now null, as a way of doing passthru on failures.
        ;
        (s = "char *") or [s = "const char *"] ["'string'"]

        ; Other pointer types aren't strings.  `unsigned char *` is a byte
        ; array, and should perhaps use ArrayBuffer.  But for now, just assume
        ; anyone working with bytes is okay calling emscripten API functions
        ; directly (e.g. see getValue(), setValue() for peeking and poking).
        ;
        ; !!! It would be nice if REBVAL* could be type safe in the API and
        ; maybe have some kind of .toString() method, so that it would mold
        ; automatically?  Maybe wrap the emscripten number in an object?
        ;
        find s "*" ["'number'"]

        ; !!! There are currently no APIs that deal in arrays directly
        ;
        find s "[" ["'array'"]

        ; !!! JavaScript has a Boolean type...figure out how to use correctly
        ;
        s = "bool" ["'Boolean'"]

        ; !!! JavaScript does not differentiate numeric types, though it does
        ; have a BigInt, which should be considered when bignum is added:
        ;
        ; https://developers.google.com/web/updates/2018/05/bigint
        ;
        find/case [
            "int"
            "unsigned int"
            "double"
            "long"
            "int64_t"
            "uint32_t"
            "size_t"
            "REBRXT"
        ] s ["'number'"]

        ; JavaScript has undefined as what `function() {return;}` returns.
        ; The differences between undefined and null are subtle and easy to
        ; get wrong, but a void-returning function should map to undefined.
        ;
        parse s ["void" any space end] ["undefined"]
    ]
]

map-each-api [
    if find [
        "rebStartup" ;-- no rebEnterApi, extra initialization in its wrapper
        "rebPromise_callback" ;-- must be called as _RL_rebPromise_callback
        "rebEnterApi_internal" ;-- called as _RL_rebEnterApi_internal
    ] name [
        continue
    ]

    js-returns: to-js-type returns else [
        fail ["No JavaScript return mapping for type" returns]
    ]

    js-param-types: collect [
        for-each [type var] paramlist [
            if type = "intptr_t" [ ;-- e.g. <promise>
                keep "'number'"
                continue
            ]
            keep to-js-type type else [
                fail ["No JavaScript argument mapping for type" type]
            ]
        ]
    ]

    if find js-param-types <va_ptr> [
        if 2 < length of js-param-types [
            print cscape/with
            "!!! WARNING! !!! Skipping mixed variadic function $<Name> !!!"
            api
            continue
        ]

        enter: copy {_RL_rebEnterApi_internal();}
        if false [
            ; It can be useful for debugging to see the API entry points;
            ; using console.error() adds a stack trace to it.
            ;
            append enter unspaced [{^/console.error("Entering } name {");}]
        ]

        return-code: if false [
            ; Similar to debugging on entry, it can be useful on exit to see
            ; when APIs return...code comes *before* the return statement.
            ;
            unspaced [{console.error("Exiting } name {");^/}]
        ] else [
            copy {}
        ]
        append return-code trim/auto copy switch js-returns [
          "'string'" [
            ;
            ; If `char *` is returned, it was rebAlloc'd and needs to be freed
            ; if it is to be converted into a JavaScript string
            {
                var js_str = UTF8ToString(a)
                rebFree(a)
                return js_str
            }
          ]
          <promise> [
            ;
            ; The promise returns an ID of what to use to write into the table
            ; for the [resolve, reject] pair.  It will run the code that
            ; will call the RL_Resolve later...after a setTimeout, so it is
            ; sure that this table entry has been entered.
            ;
            {
                return new Promise(function(resolve, reject) {
                    RL_Register(a, [resolve, reject])
                })
            }
          ]

          ; !!! Doing return and argument transformation needs more work!
          ; See suggestions: https://forum.rebol.info/t/817

          default [
            {return a}
          ]
        ]

        e-cwrap/emit cscape/with {
            $<Name> = function() {
                $<Enter>
                var argc = arguments.length
                var stack = stackSave()
                var va = allocate(4 * (argc + 1 + 1), '', ALLOC_STACK)
                var a, i, l, p
                for (i=0; i < argc; i++) {
                    a = arguments[i]
                    switch (typeof a) {
                      case 'string':
                        l = lengthBytesUTF8(a) + 4
                        l = l & ~3
                        p = allocate(l, '', ALLOC_STACK)
                        stringToUTF8(a, p, l)
                        break
                      case 'number':
                        p = a
                        break
                      default:
                        throw Error("Invalid type!")
                    }
                    HEAP32[(va>>2) + i] = p
                }

                HEAP32[(va>>2) + argc] = rebEND

                // va + 4 is where the first vararg is, must pass as *address*
                // Just put the address on the heap after the rebEND
                //
                HEAP32[(va>>2) + (argc + 1)] = va + 4

                a = _RL_$<Name>(HEAP32[va>>2], va + 4 * (argc + 1))
                stackRestore(stack)

                $<Return-Code>
            }
        } api
    ] else [
        e-cwrap/emit cscape/with {
            $<Name> = Module.cwrap(
                'RL_$<Name>',
                $<Js-Returns>, [
                    $(Js-Param-Types),
                ]
            )
        } api
    ]
]
e-cwrap/emit {
    rebStartup = function() {
        _RL_rebStartup()

        /* rebEND is a 2-byte sequence that must live at some address */
        rebEND = _malloc(2)
        setValue(rebEND, -127, 'i8') // 0x80
        setValue(rebEND + 1, 0, 'i8') // 0x00
    }

    /*
     * JS-NATIVE has a spec which is a Rebol block (like FUNC) but a body that
     * is a TEXT! of JavaScript code.  For efficiency, that text is made into
     * a function one time (as opposed to EVAL'd each time).  The function is
     * saved in this map, where the key is a stringification of the heap
     * pointer that identifies the ACTION!.
     */

    var RL_JS_NATIVES = {};

    RL_Register = function(id, fn) {
        if (id in RL_JS_NATIVES)
            throw Error("Already registered " + id + " in JS_NATIVES table")
        RL_JS_NATIVES[id] = fn
    }

    RL_Unregister = function(id) {
        if (!(id in RL_JS_NATIVES))
            throw Error("Can't delete " + id + " in JS_NATIVES table")
        delete RL_JS_NATIVES[id]
    }

    RL_Dispatch = function(id) {
        if (!(id in RL_JS_NATIVES))
            throw Error("Can't dispatch " + id + " in JS_NATIVES table")
        var result = RL_JS_NATIVES[id]()
        if (result === undefined) // no return, `return;`, `return undefined;`
            return rebVoid() // treat equivalent to VOID! value return
        else if (result === null) // explicit result, e.g. `return null;`
            return 0
        else if (Number.isInteger(result))
            return result // treat as REBVAL* heap address (should be wrapped)
        throw Error("JS-NATIVE must return null, undefined, or REBVAL*")
    }

    RL_AsyncDispatch = function(id, atomic_addr) {
        if (!(id in RL_JS_NATIVES))
            throw Error("Can't dispatch " + id + " in JS_NATIVES table")

        var resolve = function(arg) {
            if (arguments.length > 1)
                throw Error("JS-AWAITER's resolve() can only take 1 argument")
            if (arg === undefined) // `resolve()`, `resolve(undefined)`
                {} // allow it
            else if (arg === null) // explicit result, e.g. `resolve(null)`
                {} // allow it
            else if (typeof arg !== "function")
                throw Error("JS-AWAITER's resolve() only takes a function")
            RL_JS_NATIVES[atomic_addr] = arg
            setValue(atomic_addr, 1, 'i8')
        }

        var reject = function(arg) {
            throw Error("JS-AWAITER reject() not yet implemented")
        }

        var result = RL_JS_NATIVES[id](resolve, reject)
        if (result !== undefined)
            throw Error("JS-AWAITER cannot return a value, use resolve()")
    }

    RL_Await = function(atomic_addr) {
        var fn = RL_JS_NATIVES[atomic_addr]
        RL_Unregister(atomic_addr);

        if (typeof fn == "function")
            fn = fn()
        if (fn === null)
            return 0
        if (fn === undefined)
            return rebVoid()
        return fn
    }

    RL_Resolve = function(id, rebval) {
        if (!(id in RL_JS_NATIVES))
            throw Error("Can't dispatch " + id + " in JS_NATIVES table")
        RL_JS_NATIVES[id][0](rebval)
        RL_Unregister(id);
    }
}
e-cwrap/write-emitted
