REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate extention native header files"
    File: %prep-extension.r  ; EMIT-HEADER uses to indicate emitting script
    Rights: --{
        Copyright 2017 Atronix Engineering
        Copyright 2017-2025 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Needs: 2.100.100
    Description: --{
        This script is used to preprocess C source files containing code for
        extension DLLs, designed to load new native code into the interpreter.

        Such code is very similar to that of the code which is built into
        the EXE itself.  Hence, features like scanning the C comments for
        native specifications is reused.
    }--
    Notes: --{
     A. The build process distinguishes between an extension that wants to use
        just "rebol.h" vs. all of "sys-core.h".  See `use-librebol`

     B. Right now, this script takes a directory on the filesystem to say
        where the extension can be found.  Longer term, this should work if
        you want to point at extensions on the web, or a GitHub URL.  It
        should be able to fetch the sources to a temporary directory, build
        the extension, and clean up.

     C. In addition to %make-spec.r, there's also a Rebol-style header embedded
        in the comments at the top of the C source files for modules.  It's
        not clear what kind of actionable information should be put there.
        But perhaps things like what EXT_SYM_XXX are used would be better to
        define there in the source files that use them?
    }--
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>
import <platforms.r>

import <native-emitters.r>  ; scans C source for native specs, emits macros


=== "PROCESS COMMAND-LINE ARGUMENTS TO %prep-extension.r" ===

args: parse-args system.script.args  ; either from command line or DO:ARGS
platform-config: configure-platform args.OS_ID

mod: ensure text! args.MODULE  ; overwrites MOD as MODULO, but that's okay!

ext-src-dir: to file! args.DIRECTORY

sources: ensure block! load3 args.SOURCES

use-librebol: switch args.USE_LIBREBOL [
    "no" ['no]
    "yes" ['yes]
    fail "%prep-extension.r needs USE_LIBREBOL as yes or no"
]

print newline

if verbose [
    print ["Building Extension" mod "from" mold sources]
]


=== "SET UP DIRECTORIES" ===

; Scripts that are run from the makefile assume they're running from the
; directory the makefile is in (e.g. %build/), so paths for output files are
; assumed to be relative to that location.

ext-prep-subdir: cscape %prep/extensions/$<mod>/
mkdir:deep ext-prep-subdir


=== "LOAD HEADER OF %MAKE-SPEC.R FILE" ===

; The concept of the %make-spec.r file is that it's only executed once, by
; %make.r - which uses the results of running arbitrary code to generate
; the file list which goes into the makefile.  Hence anything it calculates
; dynamically has to be tunneled in to %prep-extension.r somehow (e.g.
; using the command line).
;
; But static options for the extension that do not require running arbitrary
; code can be put in the header of %make-spec.r

ext-header: first load3:header (join ext-src-dir %make-spec.r)


=== "SPECIALIZE FAIL TO REPORT EXTENSION BEING BUILT" ===

; Something like this should probably be automatic.  More thinking is needed
; on contextualizing errors better.

fail: adapt fail/ [
    print "** FAILURE WHILE PROCESSING:" join ext-src-dir %make-spec.r
]


=== "STARTUP AND SHUTDOWN HOOKS" ===

; The STARTUP* and SHUTDOWN* native functions don't actually run their
; NATIVE_CFUNC() directly, but run hook functions that contain lines of C
; that this extension gathers up.  Those hooks then call the actual STARTUP*
; and SHUTDOWN* implementations.

startup-hooks: []
shutdown-hooks: []


=== "USE PROTOTYPE PARSER TO GET NATIVE SPECS FROM COMMENTS IN C CODE" ===

; EXTRACT-NATIVE-PROTOS scans the core source for natives.  Reuse it.
;
; Note: There's also a Rebol-style header embedded in the comments at the top
; of the C module files, though it's not clear what kind of actionable
; information should be put there.

natives: []
generics: []

for-each 'file sources [
    file: join ext-src-dir file
    append natives spread extract-native-protos file
    append generics spread extract-generic-implementations file
]


=== "MAKE NATIVE STARTUP* AND SHUTDOWN* FUNCTIONS IF NONE EXIST" ===

; While the extension developer might not need any startup or shutdown code,
; this %prep-extension.r process may add code that needs to run on startup
; or shutdown, and that's done by slipstreaming the code into the natives.
; So create them automatically if they were not made.

has-startup*: null
has-shutdown*: null

for-each 'info natives [
    if info.name = "startup*" [
        if yes? info.exported [
            ;
            ; STARTUP* is supposed to be called once and only once, by the
            ; internal extension code.
            ;
            fail "Do not EXPORT the STARTUP* function for an extension!"
        ]
        has-startup*: okay
    ]
    if info.name = "shutdown*" [
        if yes? info.exported [
            ;
            ; SHUTDOWN* is supposed to be called once and only once, by the
            ; internal extension code.
            ;
            fail "Do not EXPORT the SHUTDOWN* function for an extension!"
        ]
        has-shutdown*: okay
    ]
]

if not has-startup* [
    append natives make native-info! [
        proto: --{startup*: native ["Startup extension" return: [~]]}--

        name: "startup*"
        exported: 'no
        native-type: 'normal

        file: %prep-extension.r
        line: "???"
    ]
]

if not has-shutdown* [
    append natives make native-info! [
        proto: --{shutdown*: native ["Shutdown extension" return: [~]]}--

        name: "shutdown*"
        exported: 'no
        native-type: 'normal

        file: %prep-extension.r
        line: "???"
    ]
]


=== "MAKE TEXT FROM VALIDATED NATIVE SPECS" ===

; The proto-parser does some light validation of the native specification.
; There could be some extension-specific processing on the native spec block
; or checking of refinements here.

specs-uncompressed: make text! 10000

num-natives: 0

for-each 'info natives [
    append specs-uncompressed info.proto
    append specs-uncompressed newline

    num-natives: num-natives + 1
]


=== "EMIT THE INCLUDE_PARAMS_OF_XXX MACROS FOR THE EXTENSION NATIVES" ===

include-name: cscape %tmp-mod-$<mod>.h

e1: make-emitter "Module C Header File Preface" (
    join ext-prep-subdir include-name
)

if yes? use-librebol [
    e1/emit [--{
        /* extension configuration says [use-librebol: 'yes] */

        #undef LIBREBOL_BINDING_NAME  /* defaulted by rebol.h */

        #define LIBREBOL_BINDING_NAME  librebol_binding_$<mod>

        /*
         * Define DECLARE_NATIVE macro to include extension name.
         * This avoids name collisions with the core, or with other extensions.
         *
         * The API form takes a Context*, e.g. a varlist.
         *
         * Note: The argument is currently called "level_" for consistency.
         * It may be that actual consistency is desired to pass a level_ and
         * make it possible for LIBREBOL_CONTEXT to look at the Node* and
         * tell the difference.  But right now, that name consistency is
         * important for the passthru done by STARTUP-HOOKS and SHUTDOWN-HOOKS
         */
        #define DECLARE_NATIVE(name) \
            RebolBounce N_${MOD}_##name(RebolContext* level_)

        #define NATIVE_CFUNC(name)  N_${MOD}_##name
    }--]
] else [
    e1/emit [--{
        /* extension configuration says [use-librebol: 'no] */

        #undef LIBREBOL_BINDING_NAME  /* defaulted by sys-core.h */

        #define LIBREBOL_BINDING_NAME  librebol_binding_$<mod>

        /*
         * We replace the declaration-based macros with ones that put the
         * module in them, since all the DECLARE_NATIVE() using the core
         * definitions have been included and expanded (the source would be
         * uglier if we had to write EXT_DECLARE_NATIVE() in extensions.)
         */

        #undef DECLARE_NATIVE
        #define DECLARE_NATIVE(name) \
            Bounce N_${MOD}_##name(Level* level_)

        #undef IMPLEMENT_GENERIC
        #define IMPLEMENT_GENERIC(name,type) \
            Bounce G_${MOD}_##name##_##type(Level* level_)

        /*
         * We save the definitions of NATIVE_CFUNC() and GENERIC_CFUNC() in
         * case the extension wants to use the core's definitions.  Then
         * redefine them to include the module name.
         */

        #define CORE_NATIVE_CFUNC  NATIVE_CFUNC
        #define CORE_GENERIC_CFUNC  GENERIC_CFUNC

        #undef NATIVE_CFUNC
        #define NATIVE_CFUNC(name)  N_${MOD}_##name

        #undef GENERIC_CFUNC
        #define GENERIC_CFUNC(name,type)  G_${MOD}_##name##_##type

        #define GENERIC_ENTRY(name,type) \
            g_generic_${MOD}_##name##_##type

        #define EXTENDED_HEART(name) /* name looks like Is_Image */ \
            g_extension_type_##name

        /* Note: I like using parentheses with such a macro to help notice
         * when it's used in a variable name slot that it can't be the name
         * of the variable.  But this runs up against something else I like,
         * which is to use & at callsites on arrays to emphasize that it's
         * an address being passed (even though arrays decay to pointers
         * anyway).  Something has to give as &EXTENDED_GENERICS() is being
         * interpreted as `ExtraGenericTable (*)[]` in the definition.  So
         * I just don't use the & at the callsites.  :-(
         */
        #define EXTENDED_GENERICS() \
            g_generics_${MOD}
    }--]
]

e1/emit [--{
    /*
     * This global definition is shadowed by the local definitions that
     * are picked up by APIs like `rebValue()`, to know how to look up
     * arguments to natives in frames.  It is intialized by the module
     * machinery to be the module these natives are in, which receives the
     * address of the variable via rebCollateExtension_internal().
     *
     * The consequence is that if you call a service function inside
     * your module implementation, it will be able to find definitions in
     * the module with calls to rebValue() and other APIs...but it won't
     * see the parameters of the function that called it.
     */
    extern RebolContext* LIBREBOL_BINDING_NAME;

    /*
     * Helpful warnings tell us when static variables are unused.  We
     * could turn off that warning, but instead just have the natives
     * do it before they define their own binding.  As long as at least
     * one native is in the file, this works.
     */
    #define LIBREBOL_BINDING_USED() (void)LIBREBOL_BINDING_NAME
}--]


e1/emit [--{
    #include "sys-ext.h" /* for things like DECLARE_MODULE_INIT() */
}--]


=== "FORWARD DECLARE EXT_SYM_XXX CONSTANTS AND VARIABLES" ===

; It may be possible to sometimes build an extension using librebol, and
; sometimes build it using the core.  We validate the Extended-Words: in
; the header either way, but only output them if use-librebol enabled.

ext-symids: load3 (join what-dir %prep/boot/tmp-ext-symid.r)

symbol-forward-decls: []
symbol-globals: []

for-each 'symbol maybe try ext-header.extended-words [
    if not word? symbol [
        fail ["Extended-Words entries must be WORD!:" mold symbol]
    ]

    let id: select ext-symids symbol
    if not id [
        fail [
            "Extended-Words: [" mold symbol "]"
                "must appear in" (join repo-dir %boot/ext-words.r)
        ]
    ]

    let ext-sym: cscape [symbol "EXT_SYM_${SYMBOL}"]

    append symbol-forward-decls cscape [symbol id --{
        #define $<EXT-SYM>  EXT_SYM_$<id>
        extern RebolValue* g_symbol_holder_${SYMBOL};
    }--]

    append symbol-globals cscape [symbol
        --{RebolValue* g_symbol_holder_${SYMBOL} = nullptr;}--
    ]
    append startup-hooks cscape [symbol
        --{g_symbol_holder_${SYMBOL} = Register_Symbol("$<symbol>", $<EXT-SYM>);}--
    ]
    insert shutdown-hooks cscape [symbol
        --{Unregister_Symbol(g_symbol_holder_${SYMBOL}, $<EXT-SYM>);}--
    ]
]

if not empty? symbol-forward-decls [
    if yes? use-librebol [
        fail ["Extended-Words in %make-spec.r can't be used with USE-LIBREBOL"]
    ]

    e1/emit [--{
        /*
         * #defines for Extended-Symbols: [...] in %make-spec.r header
         *
         * These are agreed-upon symbol numbers for symbols whose strings are
         * not baked into the interpreter or allocated automatically on boot.
         * But the fact that the numbers are agreed by contract means that an
         * extension can use uint16_t ID values to recognize the symbols in
         * switch() statements.
         *
         * See %boot/ext-words.r in core, and the Register_Symbol() function.
         */

        #define EXT_CANON(name)  Cell_Word_Symbol(g_symbol_holder_##name)

        $[Symbol-Forward-Decls]
    }--]
]


=== "IF NOT USING LIBREBOL, DEFINE INCLUDE_PARAMS_OF_XXX MACROS" ===

e1/emit [--{
    /*
     * INCLUDE_PARAMS_OF MACROS: DEFINING PARAM(), Bool_ARG(), ARG()
     *
     * Note these are not technically required if the extension uses
     * librebol.
     */
}--]

if yes? use-librebol [
    for-each 'info natives [
        let proto-name
        parse3 info.proto [
            opt ["export" space] proto-name: across to ":"
            to <end>
        ]
        proto-name: to-c-name proto-name

        ; We don't technically need to do anything for the INCLUDE_PARAMS_OF
        ; macros for librebol.  But we can mark the
        ; We trickily shadow the global `librebol_binding` with a version
        ; extracted from the passed-in level.
        ;
        e1/emit [info proto-name --{
            #define INCLUDE_PARAMS_OF_${PROTO-NAME} \
                LIBREBOL_BINDING_NAME = level_;
        }--]
    ]
]
else [
    for-each 'info natives [
        emit-include-params-macro e1 info.proto
    ]
]


=== "FORWARD-DECLARE DECLARE_NATIVE DISPATCHER PROTOTYPES" ===

; We need to put all the C functions that implement the extension's native
; into an array.  But those functions live in the C file for the module.
; There have to be prototypes in our `.inc` file with the arrays, in order
; to get at the addresses of those functions.

cfunc-forward-decls: collect [
    for-each 'info natives [
        keep cscape [info "DECLARE_NATIVE(${INFO.NAME})"]
    ]
]

e1/emit [cfunc-forward-decls --{
    /*
     * Forward-declare DECLARE_NATIVE()-based function prototypes
     */
    $[Cfunc-Forward-Decls];
}--]


=== "FORWARD DECLARE EXTENSION TYPES" ===

; There really should be a DECLARE_TYPE() as a module may want to define
; handlers for types from other extensions, or define multiple types, etc.
; As a hack assume just one type per extension that uses IMPLEMENT_GENERIC()

if no? use-librebol [
    e1/emit [--{
        /*
         * Current lame implementation is that if you use IMPLEMENT_GENERIC()
         * it assumes they're all for the same type, and it defines a place
         * to put the ExtraHeart* that comes back from registering it.  The
         * table passed to Register_Generics() holds a pointer to this
         * pointer, so when the Register_Datatype() puts a value into that
         * space it can be propagated to the generic entries getting linked.
         */
    }--]

    if not empty? generics [
        let Is_Xxx: generics.1.proper-type
        e1/emit [info --{
            extern ExtraHeart* EXTENDED_HEART(${Is_Xxx});
        }--]

        e1/emit [info --{
            INLINE bool ${Is_Xxx}(const Cell* v) {
                if (not Type_Of_Is_0(v))
                    return false;
                return Cell_Extra_Heart(v) == EXTENDED_HEART(${Is_Xxx});
            }
        }--]
    ]
]


=== "FORWARD DECLARE ExtraGenericInfo FOR GENERIC IMPLEMENTATIONS" ===

; Non-built-in generics are registered as a linked list for each generic.
; Each element in the linked list is a global at a fixed address, so no
; dynamic allocations are necessary to build the list.

if no? use-librebol [
    for-each 'info generics [
        e1/emit [info
            -{IMPLEMENT_GENERIC(${INFO.NAME}, ${Info.Proper-Type});}-
        ]
    ]

    for-each 'info generics [
        e1/emit [info --{
            extern ExtraGenericInfo GENERIC_ENTRY(${INFO.NAME}, ${Info.Proper-Type});
        }--]
    ]

    e1/emit [--{
        extern const ExtraGenericTable (EXTENDED_GENERICS())[];  /* name macro! */
    }--]
]

e1/write-emitted


=== "MAKE AGGREGATED SCRIPT FROM HEADER, NATIVE SPECS, AND INIT CODE" ===

; The module is created with an IMPORT* call on one big blob of script.  That
; script is blended together and looks like:
;
;    Rebol [
;        Title: "This header is whatever was in the %ext-xxx-init.reb"
;        Type: module
;        ...
;    ]
;
;    ; These native specs are extracted from the C comments in the extension
;    ; They must be in the same order as `cfunc-names`, because that's
;    ; the array that NATIVE steps through on each call to find the C code!
;    ;
;    export alpha: native [...]  ; v-- see EXPORT note below
;    beta: infix native [...]
;
;    ; The rest of the code was the body of %ext-xxx-init.reb
;    ;
;    something: <value>
;    gamma: func [...] [alpha ..., beta ...]
;
; At the moment this script is executed, the system has internal state that
; lets it make each successive call to NATIVE pick the next native out of
; the creation queue.  That's a bit of voodoo, but the user doesn't see it...
; and there's really no way to do this that *isn't* voodoo at some level.
;
; Note: We could do something like gather the exports on natives up and put
; them in the header.  But the special variadic recognition of EXPORT is an
; implemented feature so we use it.  Prior to variadics, this syntax had been
; proposed for R3-Alpha, implemented by MODULE scanning its body:
;
;   http://www.rebol.net/r3blogs/0300.html
;
; Note: Because we are always passing valid UTF-8, we can potentially take
; advantage of that during the scan.  (We don't yet, but could.)  Hence the
; number of codepoints in the string are passed in, as that's required to
; have a validated TEXT!...which is how we'd signal validity to the scanner.

e: make-emitter "Extension Initialization Script Code" (
    join ext-prep-subdir cscape %tmp-mod-$<mod>-init.c
)


=== "GENERATE MODULE'S TMP-XXX-INIT.C FILE" ===

if yes? use-librebol [
    e/emit [--{
        #include <assert.h>
        #include "rebol.h"
    }--]
] else [
    e/emit [--{#include "sys-core.h"}--]
]

e/emit [--{
    #include "tmp-mod-$<mod>.h" /* for DECLARE_NATIVE() forward decls */

    /*
     * See comments on RebolApiTable, and how it is used to pass an API table
     * from the executable to the extension (this works cross platform, while
     * things like "import libraries for an EXE that a DLL can import" are
     * Windows peculiarities).
     */
    #ifdef LIBREBOL_USES_API_TABLE  /* e.g. a DLL */
        RebolApiTable* g_librebol;  /* API macros like rebValue() use this */
    #endif

    /*
     * When the extension is loaded, this global is set to the module's
     * pointer.  It means that lookups will be done in that module when you
     * call things like rebValue() etc.
     */
    RebolContext* LIBREBOL_BINDING_NAME;
}--]

if not has-startup* [
    e/emit [--{
        DECLARE_NATIVE(STARTUP_P)  /* auto-generated since not in extension */
        {
            INCLUDE_PARAMS_OF_STARTUP_P;
            return rebNothing();
        }
    }--]
]

if not has-shutdown* [
    e/emit [--{
        DECLARE_NATIVE(SHUTDOWN_P)  /* auto-generated since not in extension */
        {
            INCLUDE_PARAMS_OF_SHUTDOWN_P;
            return rebNothing();
        }
    }--]
]

if not empty? symbol-globals [
    e/emit [--{
        /*
         * Globals with storage for Extended-Symbols: from %make-spec.r
         */

        $[Symbol-Globals]
    }--]
]

if not empty? generics [
    e/emit [--{
        /*
         * This is a hacky way of declaring generic types, which is evolving.
         *
         * Basically we assume if IMPLEMENT_GENERIC() is used in an extension
         * then it used for a type that extension defines.  If we see
         * IMPLEMENT_GENERIC(SOMETHING, Is_Xxx) then a datatype is created
         * for xxx!  This needs lots of work.
         */
    }--]

    let proper-type: generics.1.proper-type

    let type
    parse3 proper-type ["Is_" type: across to <end>]
    type: append lowercase type "!"

    e/emit [info --{
        ExtraHeart* EXTENDED_HEART(${Proper-Type}) = nullptr;
    }--]

    append startup-hooks cscape [proper-type type
        --{EXTENDED_HEART(${Proper-Type}) = Register_Datatype("$<type>");}--
    ]

    insert shutdown-hooks cscape [proper-type
        --{Unregister_Datatype(EXTENDED_HEART(${Proper-Type}));}--
    ]
]

if not empty? generics [

    e/emit [--{
        /*
        * These are the static globals that are used as the linked list entries
        * when the extension registers its generics.  No dynamic allocations are
        * needed--the pointers in the globals are simply updated.
        */
    }--]

    for-each 'info generics [
        e/emit [info --{
            ExtraGenericInfo GENERIC_ENTRY(${INFO.NAME}, ${Info.Proper-Type}) = {
                nullptr,  /* replaced with *ExtraGenericTable.ext_heart_ptr */
                GENERIC_CFUNC(${INFO.NAME}, ${Info.Proper-Type}),
                nullptr  /* used as pointer for next in linked list */
            };
        }--]
    ]

    table-items: collect [
        for-each 'info generics [
            let table: cscape [info
                "&GENERIC_TABLE(${INFO.NAME})"
            ]
            let ext_info: cscape [info
                "&GENERIC_ENTRY(${INFO.NAME}, ${Info.Proper-Type})"
            ]
            let ext_heart_ptr: cscape [info
                "&EXTENDED_HEART(${Info.Proper-Type})"
            ]

            keep cscape [table ext_info ext_heart_ptr
                "{ $<Table>, $<Ext_Info>, $<Ext_Heart_Ptr> }"
            ]
        ]
        keep --{{ nullptr, nullptr, nullptr }}--
    ]

    ; Micro-optimization could avoid making this if no IMPLEMENT_GENERIC()s.
    ; But the code has fewer branches and edge cases if we always make it.
    ;
    e/emit [--{
        /*
        * This table is passed to Register_Generics() and Unregister_Generics().
        * It maps from the generic tables to the entry that is added and removed
        * from the linked list of the table in that particular generic for the
        * non-built-in types.
        */
        const ExtraGenericTable (EXTENDED_GENERICS())[] = {  /* name macro! */
            $[Table-Items],
        };
    }--]

    append startup-hooks "Register_Generics(EXTENDED_GENERICS());"
    insert shutdown-hooks "Unregister_Generics(EXTENDED_GENERICS());"
]

if empty? startup-hooks [
    append startup-hooks "/* no startup-hooks */"
]

if empty? shutdown-hooks [
    append shutdown-hooks "/* no shutdown-hooks */"
]

e/emit [--{
    /*
     * Hooked implementation functions for STARTUP* and SHUTDOWN*
     *
     * Based on various features that %prep-extension.r wants to build in
     * automatically, it needs to run some C code on startup and shutdown
     * of the extension.  Instead of finding some way to tuck CFunctions
     * that implement this behavior in a place the extension machinery can
     * find it, this slips the code into the STARTUP* and SHUTDOWN* natives
     * themselves... by replacing their native functions in the registration
     * table with functions that add in additional code.
     *
     * When calls are chained through to the original native implementations,
     * the STARTUP* runs the hooks before the original code, while SHUTDOWN*
     * runs the hooks after the original code.  This means the auto setup
     * information is available during both the STARTUP* and SHUTDOWN* native
     * implementations.
     */

    static DECLARE_NATIVE(STARTUP_HOOKED) {
        $[Startup-Hooks]

        return NATIVE_CFUNC(STARTUP_P)(level_);  /* STARTUP* (after hooks) */
    }

    static DECLARE_NATIVE(SHUTDOWN_HOOKED) {
        RebolBounce bounce = NATIVE_CFUNC(SHUTDOWN_P)(level_);  /* SHUTDOWN* */

        $[Shutdown-Hooks]

        return bounce;  /* result from running SHUTDOWN* (before hooks) */
    }
}--]

cfunc-names: collect [  ; must be in the order that NATIVE is called!
    for-each 'info natives [
        let name: info.name
        case [
            name = "startup*" [name: "startup-hooked"]
            name = "shutdown*" [name: "shutdown-hooked"]
        ]
        keep cscape [mod name "N_${MOD}_${NAME}"]
    ]
]

e/emit [--{
    /*
     * Pointers to function dispatchers for natives (in same order as the
     * order of native specs after being loaded).  Cast is used so that if
     * an extension uses "rebol.h" and doesn't return a Bounce C++ class it
     * should still work (it's a standard layout type).
     *
     * These may be Dispatcher* or RebolActionCFunction* depending on
     * whether use-librebol is yes or no.  The former takes a Level* in order
     * to be able to work with intrinsics that don't have varlists (or that
     * don't necessarily manage varlists and link them virtually for lookup),
     * and the latter takes a Context* which is always a varlist and has
     * always been managed and set up for lookup.
     *
     * 1. This is really just `CFunction* native_cfuncs[...]`, but since the
     *    CFunction is defined in %c-enhanced.h, we don't know all API
     *    clients will have it available.  Rather than make up some proxy
     *    name for CFunction that contaminates the interface, assume people
     *    can use AI to ask what this means if they can't read it.  See the
     *    definition of CFunction for why we can't just use void* here.
     */
    static void (*native_cfuncs[$<num-natives> + 1])(void) = {  /* ick [1] */
        (void (*)(void))$[Cfunc-Names],  /* cast to ick [1] */
        0  /* just here to ensure > 0 length array (language requirement) */
    };
}--]


=== "EMIT COMPRESSED STARTUP SCRIPT CODE AS C BYTE ARRAY" ===

script-name: cscape %ext-$<mod>-init.reb

header: ~
initscript-body: stripload:header (join ext-src-dir script-name) $header
ensure text! header  ; stripload gives back textual header

script-uncompressed: cscape [--{
    Rebol [
        $<Header>
    ]

    ; These NATIVE invocations execute to definte the natives, implicitly
    ; picking up a CFunction from an array in the extension.  The order of
    ; these natives must match the order of that array (each call to NATIVE
    ; advances a global pointer into that array)

    $<Specs-Uncompressed>

    startup*  ; STARTUP* will be declared automatically if not in extension

    $<Initscript-Body>
}--]

write (join ext-prep-subdir %script-uncompressed.r) script-uncompressed

script-compressed: gzip script-uncompressed
script-num-codepoints: length of script-uncompressed
script-len: length of script-compressed

e/emit [--{
    /*
     * Gzip compression of $<Script-Name>
     * Originally $<length of script-uncompressed> bytes
     */
    static const unsigned char script_compressed[$<script-len>] = {
    $<Binary-To-C:Indent Script-Compressed 4>
    };
}--]


=== "EMIT COLLATED STRUCTURE DESCRIBING EXTENSION" ===

e/emit [--{
    /*
     * Hook called by the core to gather all the details of the extension up
     * so the system can process it.  This hook doesn't decompress any of the
     * code itself or run any initialization routines--this allows for
     * deferred loading.  While that's not particularly useful for DLLs (why
     * load the DLL unless you're going to initialize the extension?) it can
     * be useful for built-in extensions in EXEs or libraries, so a list can
     * be available in the binary but only load individual ones on demand.
     *
     * !!! At the moment this code returns a BLOCK!, though having it return
     * an ACTION! which can initialize or shutdown the extension as a black
     * box or interface could provide more flexibility for arbitrary future
     * extension implementations.
     */
    DECLARE_EXTENSION_COLLATOR(${Mod}) {
      #ifdef LIBREBOL_USES_API_TABLE
        /*
         * Librebol extensions use `rebXXX()` APIs => `g_librebol->rebXXX()`
         *
         * (Core extensions transform rebXXX() => direct RL_rebXXX() calls)
         */
        g_librebol = api;
      #else
        assert(api == 0);
        (void)api;  /* USED(api) to prevent warning in release builds */
      #endif

        return rebCollateExtension_internal(
            &LIBREBOL_BINDING_NAME,  /* where to put module pointer on init */
            $<either yes? use-librebol ["true"] ["false"]>,  /* CFunc type */
            script_compressed,  /* script compressed data */
            sizeof(script_compressed),  /* size of script compressed data */
            $<script-num-codepoints>,  /* codepoints in uncompressed utf8 */
            native_cfuncs,  /* C function pointers for native implementations */
            $<num-natives>  /* number of NATIVE invocations in script */
        );
    }
}--]

e/write-emitted

print newline
