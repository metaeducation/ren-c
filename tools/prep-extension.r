REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate extention native header files"
    File: %prep-extension.r  ; EMIT-HEADER uses to indicate emitting script
    Rights: --{
        Copyright 2017 Atronix Engineering
        Copyright 2017-2021 Ren-C Open Source Contributors
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
        just "rebol.h" vs. all of "rebol-internals.h".  See `use-librebol`

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

mod: ensure text! args.MODULE

directory: to file! args.DIRECTORY

sources: ensure block! load3 args.SOURCES

use-librebol: switch args.USE_LIBREBOL [
    "no" ['no]
    "yes" ['yes]
    fail "%prep-extension.r needs USE_LIBREBOL as yes or no"
]


=== "CALCULATE NAMES OF BUILD PRODUCTS" ===

; Assume we start up in the directory where build products are being made,
; e.g. %build/
;
; !!! We put the module's .h files and the init.c file for the initialization
; code into the %prep/<name-of-extension> directory, which is added to the
; include path for the build of the extension

m-name: mod
l-m-name: lowercase copy m-name
u-m-name: uppercase copy m-name

print ["Building Extension" m-name "from" mold sources]

output-dir: join what-dir [%prep/extensions/ l-m-name "/"]
mkdir:deep output-dir

script-name: join directory ["ext-" l-m-name "-init.reb"]
init-c-name: to file! unspaced ["tmp-mod-" l-m-name "-init.c"]


=== "USE PROTOTYPE PARSER TO GET NATIVE SPECS FROM COMMENTS IN C CODE" ===

; EXTRACT-NATIVE-PROTOS scans the core source for natives.  Reuse it.
;
; Note: There's also a Rebol-style header embedded in the comments at the top
; of the C module files, though it's not clear what kind of actionable
; information should be put there.

natives: []
generics: []

for-each 'file sources [
    file: join directory file
    append natives spread extract-native-protos file
    append generics spread extract-generic-implementations file
]


=== "COUNT NATIVES AND DETERMINE IF THERE IS A NATIVE STARTUP* FUNCTION" ===

; If there is a native startup function, we want to call it while the module
; is being initialized...after the natives are loaded but before the Rebol
; code gets a chance to run.  So this prep code has to insert that call.

has-startup*: 'no

num-natives: 0
for-each 'info natives [
    if info.name = "startup*" [
        if yes? info.exported [
            ;
            ; STARTUP* is supposed to be called once and only once, by the
            ; internal extension code.
            ;
            fail "Do not EXPORT the STARTUP* function for an extension!"
        ]
        has-startup*: 'yes
    ]
    if info.name = "shutdown*" [
        if yes? info.exported [
            ;
            ; SHUTDOWN* is supposed to be called once and only once, by the
            ; internal extension code.
            ;
            fail "Do not EXPORT the SHUTDOWN* function for an extension!"
        ]
    ]
    num-natives: num-natives + 1
]


=== "MAKE TEXT FROM VALIDATED NATIVE SPECS" ===

; The proto-parser does some light validation of the native specification.
; There could be some extension-specific processing on the native spec block
; or checking of refinements here.

specs-uncompressed: make text! 10000

for-each 'info natives [
    append specs-uncompressed info.proto
    append specs-uncompressed newline
]


=== "EMIT THE INCLUDE_PARAMS_OF_XXX MACROS FOR THE EXTENSION NATIVES" ===

e1: make-emitter "Module C Header File Preface" (
    join output-dir ["tmp-mod-" (l-m-name) ".h"]
)

if yes? use-librebol [
    e1/emit [--{
        /* extension configuration says [use-librebol: 'yes] */

        #define LIBREBOL_BINDING_NAME  librebol_binding_$<l-m-name>
        #include "rebol.h"  /* not %rebol-internals.h ! */

        /*
         * Define DECLARE_NATIVE macro to include extension name.
         * This avoids name collisions with the core, or with other extensions.
         *
         * The API form takes a Context*, e.g. a varlist.  Its name should be
         * whatever the LIBREBOL_BINDING_NAME macro expands to (can't be 0).
         */
        #define DECLARE_NATIVE(name) \
            RebolBounce N_${MOD}_##name(RebolContext* LIBREBOL_BINDING_NAME)

    }--]
] else [
    e1/emit [--{
        /* extension configuration says [use-librebol: 'no] */

        #define LIBREBOL_BINDING_NAME  librebol_binding_$<l-m-name>
        #include "sys-core.h"  /* superset of %rebol.h */

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
                LIBREBOL_BINDING_USED()
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

e: make-emitter "Ext custom init code" (join output-dir init-c-name)

header: ~
initscript-body: stripload:header script-name $header
ensure text! header  ; stripload gives back textual header

script-uncompressed: unspaced [
    "Rebol" space "["  ; header has no brackets
        header newline   ; won't actually be indented (indents were stripped)
    "]" newline
    newline
    specs-uncompressed newline

    ; We put a call to the STARTUP* native if there was one, so it runs before
    ; the rest of the body.  (The user could do this themselves, but it makes
    ; things read better to do it automatically.)
    ;
    if yes? has-startup* [unspaced ["startup*" newline]]

    initscript-body
]
script-num-codepoints: length of script-uncompressed

write (join output-dir %script-uncompressed.r) script-uncompressed

script-compressed: gzip script-uncompressed

cfunc-names: collect [  ; must be in the order that NATIVE is called!
    for-each 'info natives [
        keep cscape [mod info "N_${MOD}_${INFO.NAME}"]
    ]
]

script-len: length of script-compressed

e/emit [--{
    #include "assert.h"
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

    /*
     * Gzip compression of $<Script-Name>
     * Originally $<length of script-uncompressed> bytes
     */
    static const unsigned char script_compressed[$<script-len>] = {
    $<Binary-To-C:Indent Script-Compressed 4>
    };

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

if no? use-librebol [  ; you can't write generics with librebol... yet!

    if not empty? generics [
        let info: generics.1
        e/emit [info --{
            ExtraHeart* EXTENDED_HEART(${Info.Proper-Type}) = nullptr;
        }--]
    ]

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
]

e/write-emitted
