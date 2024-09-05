REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate extention native header files"
    File: %prep-extension.r  ; EMIT-HEADER uses to indicate emitting script
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2021 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
    Description: {
        This script is used to preprocess C source files containing code for
        extension DLLs, designed to load new native code into the interpreter.

        Such code is very similar to that of the code which is built into
        the EXE itself.  Hence, features like scanning the C comments for
        native specifications is reused.
    }
    Notes: {
        The build process distinguishes between an extension that wants to use
        just "rebol.h" vs. using all of "rebol-internals.h".
    }
]

if not find (words of :import) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>
import <platforms.r>

import <native-emitters.r>  ; scans C source for native specs, emits macros


; !!! We put the modules .h files and the .inc file for the initialization
; code into the %prep/<name-of-extension> directory, which is added to the
; include path for the build of the extension

args: parse-args system.script.args  ; either from command line or DO/ARGS

; !!! At time of writing, SRC=extensions/name/mod-name.c is what this script
; gets on the command line.  This is split out to make a directory to put the
; prep products in, and then assumed to be in the repo's source directory.
; Longer term, this should work if you want to point at extensions with web
; addresses to pull and build them, etc.  It should not give the module name,
; just point at a directory and follow the specification.
;
src: to file! :args.SRC
in-dir: split-path3/file src inside [] 'file-name

; Assume we start up in the directory where build products are being made
;
output-dir: join what-dir spread reduce [%prep/ in-dir]

src: join repo-dir src

mkdir/deep output-dir


platform-config: configure-platform args/OS_ID

use-librebol: switch args/USE_LIBREBOL [
    "no" ['no]
    "yes" ['yes]
    fail "%prep-extension.r needs USE_LIBREBOL as yes or no"
]

mod: ensure text! args/MODULE
m-name: mod
l-m-name: lowercase copy m-name
u-m-name: uppercase copy m-name

c-src: join repo-dir (as file! ensure text! args/SRC)


=== "CALCULATE NAMES OF BUILD PRODUCTS" ===

; !!! This would be a good place to explain what the output goal of this
; script is.

print ["building" m-name "from" c-src]

script-name: copy c-src
parse3/match script-name [
    some [thru "/"]
    change "mod-" ("ext-")
    to "."
    change "." ("-init.")
    change ["c" <end> | "cpp" <end>] ("reb")
] else [
    fail [
        "Extension main file should have naming pattern %mod-xxx.c(pp),"
        "and Rebol initialization should be %ext-xxx-init.reb"
    ]  ; auto-generating version of initial (and poor) manual naming scheme
]

split-path3/file c-src inside [] 'inc-name
parse3/match inc-name [
    change "mod-" ("tmp-mod-")
    to "."
    change "." ("-init.")
    change ["c" <end> | "cpp" <end>] ("c")  ; !!! Keep as .cpp if it is?
] else [
    fail [
        "Extension main file should have naming pattern %mod-xxx.c(pp),"
        "so extension init code generates as %tmp-mod-xxx-init.c"
    ]  ; auto-generating version of initial (and poor) manual naming scheme
]


=== "USE PROTOTYPE PARSER TO GET NATIVE SPECS FROM COMMENTS IN C CODE" ===

; EXTRACT-NATIVE-PROTOS scans the core source for natives.  Reuse it.
;
; Note: There's also a Rebol-style header embedded in the comments at the top
; of the C module file, though it's not clear what kind of actionable
; information should be put there.

all-protos: extract-native-protos c-src


=== "COUNT NATIVES AND DETERMINE IF THERE IS A NATIVE STARTUP* FUNCTION" ===

; If there is a native startup function, we want to call it while the module
; is being initialized...after the natives are loaded but before the Rebol
; code gets a chance to run.  So this prep code has to insert that call.

has-startup*: 'no

num-natives: 0
for-each info all-protos [
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

for-each info all-protos [
    append specs-uncompressed info.proto
    append specs-uncompressed newline
]


=== "EMIT THE INCLUDE_PARAMS_OF_XXX MACROS FOR THE EXTENSION NATIVES" ===

e1: make-emitter "Module C Header File Preface" (
    join output-dir spread reduce ["tmp-mod-" (l-m-name) ".h"]
)

if yes? use-librebol [
    e1/emit [{
        /* extension configuration says [use-librebol: 'yes] */

        #define LIBREBOL_SPECIFIER (&librebol_specifier)
        #include "rebol.h"  /* not %rebol-internals.h ! */

        /*
         * This global definition is shadowed by the local definitions that
         * are picked up by APIs like `rebValue()`, to know how to look up
         * arguments to natives in frames.  Right now, being nullptr indicates
         * to use the stack to find which module the native is running in.
         * This needs to be revisited.
         */
        static RebolSpecifier* librebol_specifier = 0;  /* nullptr */

        /*
         * Helpful warnings tell us when static variables are unused.  We
         * could turn off that warning, but instead just have the natives
         * do it before they define their own specifier.  As long as at least
         * one native is in the file, this works.
         */
        #define LIBREBOL_SPECIFIER_USED() (void)librebol_specifier
    }]
] else [
    e1/emit [{
        /* extension configuration says [use-librebol: 'no] */
        #include "rebol-internals.h"  /* superset of %rebol.h */

        /*
         * No specifier used currently for core API extensions, but need the
         * macro for the module init to compile.
         */
        #define LIBREBOL_SPECIFIER_USED()
    }]
]
e1/emit newline

e1/emit [{
    /*
     * Define DECLARE_NATIVE macro to include extension name.
     * This avoids name collisions with the core, or with other extensions.
     */
    #define DECLARE_NATIVE(name) \
        RebolBounce N_${MOD}_##name(RebolLevel* level_)
}]
e1/emit newline

e1/emit {
    #include "sys-ext.h" /* for things like DECLARE_MODULE_INIT() */
}
e1/emit newline


=== "IF NOT USING LIBREBOL, DEFINE INCLUDE_PARAMS_OF_XXX MACROS" ===

e1/emit {
    /*
    ** INCLUDE_PARAMS_OF MACROS: DEFINING PARAM(), REF(), ARG()
    */
}
e1/emit newline

if yes? use-librebol [
    for-each info all-protos [
        parse3 info.proto [
            opt ["export" space] proto-name: across to ":"
            to <end>
        ]
        proto-name: to-c-name proto-name

        ; We trickily shadow the global `librebol_specifier` with a version
        ; extracted from the passed-in level.
        ;
        e1/emit [info {
            #define INCLUDE_PARAMS_OF_${PROTO-NAME} \
                LIBREBOL_SPECIFIER_USED();  /* global, before local define */ \
                RebolSpecifier* librebol_specifier; \
                librebol_specifier = rebSpecifierFromLevel_internal(level_); \
                LIBREBOL_SPECIFIER_USED();  /* local, after shadowing */
        }]
        e1/emit newline
    ]
]
else [
    for-each info all-protos [
        emit-include-params-macro e1 info/proto
        e1/emit newline
    ]
]


=== "FORWARD-DECLARE DECLARE_NATIVE DISPATCHER PROTOTYPES" ===

; We need to put all the C functions that implement the extension's native
; into an array.  But those functions live in the C file for the module.
; There have to be prototypes in our `.inc` file with the arrays, in order
; to get at the addresses of those functions.

dispatcher-forward-decls: collect [
    for-each info all-protos [
        name: info.name
        if info.native-type = 'intrinsic [  ; not that hard to do if needed
            fail "Intrinsics not currently supported in extensions"
        ]
        keep cscape [name {DECLARE_NATIVE(${Name})}]
    ]
]

e1/emit [dispatcher-forward-decls {
    /*
     * Forward-declare DECLARE_NATIVE() dispatcher prototypes
     */
    $[Dispatcher-Forward-Decls];
}]
e1/emit newline

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
;    ; They must be in the same order as `dispatcher_c_names`, because that's
;    ; the array that NATIVE steps through on each call to find the C code!
;    ;
;    export alpha: native [...]  ; v-- see EXPORT note below
;    beta: enfix native [...]
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

e: make-emitter "Ext custom init code" (join output-dir inc-name)

initscript-body: stripload/header script-name inside [] 'header
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

dispatcher_c_names: collect [  ; must be in the order that NATIVE is called!
    for-each info all-protos [
        name: info.name
        keep cscape [mod name {N_${MOD}_${Name}}]
    ]
]

script-len: length of script-compressed

e/emit [{
    #include "assert.h"
    #include "tmp-mod-$<mod>.h" /* for DECLARE_NATIVE() forward decls */

    /*
     * We may be only including "rebol.h" and not "rebol-internals.h", in
     * which case CFunction is not defined.
     */
    #if defined(_WIN32)  /* 32-bit or 64-bit windows */
        typedef void (__cdecl CFunction_ext)(void);
    #else
        typedef void (CFunction_ext)(void);
    #endif

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
     * Gzip compression of $<Script-Name> (no \0 terminator in array)
     * Originally $<length of script-uncompressed> bytes
     */
    static const unsigned char script_compressed[$<script-len>] = {
        $<Binary-To-C Script-Compressed>
    };

    /*
     * Pointers to function dispatchers for natives (in same order as the
     * order of native specs after being loaded).  Cast is used so that if
     * an extension uses "rebol.h" and doesn't return a Bounce C++ class it
     * should still work (it's a standard layout type).
     */
    static CFunction_ext* native_cfuncs[$<num-natives> + 1] = {
        (CFunction_ext*)$[Dispatcher_C_Names],
        0  /* just here to ensure > 0 length array (C++ requirement) */
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
        /*
         * Compiler will warn if static librebol_specifier is defined w/o use.
         */
        LIBREBOL_SPECIFIER_USED();

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
            script_compressed,  /* script compressed data */
            sizeof(script_compressed),  /* size of script compressed data */
            $<script-num-codepoints>,  /* codepoints in uncompressed utf8 */
            native_cfuncs,  /* C function pointers for native implementations */
            $<num-natives>  /* number of NATIVE invocations in script */
        );
    }
}]

e/write-emitted
