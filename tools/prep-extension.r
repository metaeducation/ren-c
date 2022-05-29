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
        Currently the build process does not distinguish between an extension
        that wants to use just "rebol.h" and one that depends on "sys-core.h"
        Hence it includes things like ARG() and REF() macros, which access
        frame internals that do not currently go through the libRebol API.

        It should be possible to build an extension that does not use the
        internal API at all, as well as one that does, so that needs review.
    }
]

verbose: false

if not find words of :import [product] [  ; See %import-shim.r
    do load append copy system/script/path %import-shim.r
]

import <common.r>
import <bootstrap-shim.r>
import <common-emitter.r>
import <systems.r>

import <native-emitters.r>  ; scans C source for native specs, emits macros


; !!! We put the modules .h files and the .inc file for the initialization
; code into the %prep/<name-of-extension> directory, which is added to the
; include path for the build of the extension

args: parse-args system/script/args  ; either from command line or DO/ARGS

; !!! At time of writing, SRC=extensions/name/mod-name.c is what this script
; gets on the command line.  This is split out to make a directory to put the
; prep products in, and then assumed to be in the repo's source directory.
; Longer term, this should work if you want to point at extensions with web
; addresses to pull and build them, etc.  It should not give the module name,
; just point at a directory and follow the specification.
;
src: to file! :args/SRC
path+file: split-path src
in-dir: path+file/1
file-name: path+file/2

; Assume we start up in the directory where build products are being made
;
output-dir: join what-dir reduce [%prep/ in-dir]

src: join repo-dir src

mkdir/deep output-dir


config: config-system try get 'args/OS_ID

mod: ensure text! args/MODULE
m-name: mod
l-m-name: lowercase copy m-name
u-m-name: uppercase copy m-name

c-src: join repo-dir (as file! ensure text! args/SRC)


=== {CALCULATE NAMES OF BUILD PRODUCTS} ===

; !!! This would be a good place to explain what the output goal of this
; script is.

print ["building" m-name "from" c-src]

script-name: copy c-src
parse2 script-name [
    some [thru "/"]
    change "mod-" ("ext-")
    to "."
    change "." ("-init.")
    change ["c" end | "cpp" end] ("reb")
] else [
    fail [
        "Extension main file should have naming pattern %mod-xxx.c(pp),"
        "and Rebol initialization should be %ext-xxx-init.reb"
    ]  ; auto-generating version of initial (and poor) manual naming scheme
]

inc-name: second split-path c-src
is-cpp: false
parse2 inc-name [
    change "mod-" ("tmp-mod-")
    to "."
    change "." ("-init.")
    change ["c" end | "cpp" end] ("c")  ; !!! Keep as .cpp if it is?
] else [
    fail [
        "Extension main file should have naming pattern %mod-xxx.c(pp),"
        "so extension init code generates as %tmp-mod-xxx-init.c"
    ]  ; auto-generating version of initial (and poor) manual naming scheme
]


=== {USE PROTOTYPE PARSER TO GET NATIVE SPECS FROM COMMENTS IN C CODE} ===

; EXTRACT-NATIVE-PROTOS scans the core source for natives.  Reuse it.
;
; Note: There's also a Rebol-style header embedded in the comments at the top
; of the C module file, though it's not clear what kind of actionable
; information should be put there.

all-protos: extract-native-protos c-src


=== {COUNT NATIVES AND DETERMINE IF THERE IS A NATIVE STARTUP* FUNCTION} ===

; If there is a native startup function, we want to call it while the module
; is being initialized...after the natives are loaded but before the Rebol
; code gets a chance to run.  So this prep code has to insert that call.

has-startup*: false

num-natives: 0
for-each info all-protos [
    if info/name = "startup*" [
        if info/exported [
            ;
            ; STARTUP* is supposed to be called once and only once, by the
            ; internal extension code.
            ;
            fail "Do not EXPORT the STARTUP* function for an extension!"
        ]
        has-startup*: true
    ]
    if info/name = "shutdown*" [
        if info/exported [
            ;
            ; SHUTDOWN* is supposed to be called once and only once, by the
            ; internal extension code.
            ;
            fail "Do not EXPORT the SHUTDOWN* function for an extension!"
        ]
    ]
    num-natives: num-natives + 1
]


=== {MAKE TEXT FROM VALIDATED NATIVE SPECS} ===

; The proto-parser does some light validation of the native specification.
; There could be some extension-specific processing on the native spec block
; or checking of refinements here.

specs-uncompressed: make text! 10000

for-each info all-protos [
    append specs-uncompressed info/proto
    append specs-uncompressed newline
]


=== {EMIT THE INCLUDE_PARAMS_OF_XXX MACROS FOR THE EXTENSION NATIVES} ===

e1: make-emitter "Module C Header File Preface" (
    join output-dir reduce ["tmp-mod-" (l-m-name) ".h"]
)

e1/emit {
    #include "sys-ext.h" /* for things like DECLARE_MODULE_INIT() */

    /*
    ** INCLUDE_PARAMS_OF MACROS: DEFINING PARAM(), REF(), ARG()
    */
}
e1/emit newline

for-each info all-protos [
    emit-include-params-macro/ext e1 info/proto u-m-name
    e1/emit newline
]


=== {FORWARD-DECLARE REBNATIVE DISPATCHER PROTOTYPES} ===

; We need to put all the C functions that implement the extension's native
; into an array.  But those functions live in the C file for the module.
; There have to be prototypes in our `.inc` file with the arrays, in order
; to get at the addresses of those functions.

dispatcher-forward-decls: collect [
    for-each info all-protos [
        name: info/name
        keep cscape/with {REBNATIVE(${Name})} 'name
    ]
]
e1/emit 'mod {
    /*
     * Redefine REBNATIVE macro locally to include extension name.
     * This avoids name collisions with the core, or with other extensions.
     */
    #undef REBNATIVE
    #define REBNATIVE(n) \
        const REBVAL *N_${MOD}_##n(REBFRM *frame_)

    /*
     * Forward-declare REBNATIVE() dispatcher prototypes
     */
    $[Dispatcher-Forward-Decls];
}
e1/emit newline

e1/write-emitted


=== {MAKE AGGREGATED SCRIPT FROM HEADER, NATIVE SPECS, AND INIT CODE} ===

; The module is created with an IMPORT* call on one big blob of script.  That
; script is blended together and looks like:
;
;    Rebol [
;        Title: {This header is whatever was in the %ext-xxx-init.reb}
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

initscript-body: stripload/header script-name 'header  ; header will be TEXT!

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
    if has-startup* [unspaced ["startup*" newline]]

    initscript-body
]
script-num-codepoints: length of script-uncompressed

write (join output-dir %script-uncompressed.r) script-uncompressed

script-compressed: gzip script-uncompressed

dispatcher_c_names: collect [  ; must be in the order that NATIVE is called!
    for-each info all-protos [
        name: info/name
        keep cscape/with {N_${MOD}_${Name}} [mod name]
    ]
]

e/emit 'mod {
    #include "sys-core.h" /* !!! Could this just use "rebol.h"? */

    #include "tmp-mod-$<mod>.h" /* for REBNATIVE() forward decls */

    /*
     * See comments on RL_LIB in %make-reb-lib.r, and how it is used to pass
     * an API table from the executable to the extension (this works cross
     * platform, while things like "import libraries for an EXE that a DLL
     * can import" are Windows peculiarities.
     */
    #ifdef REB_EXT  /* e.g. a DLL */
        RL_LIB *RL;  /* is passed to the RX_Collate() function */
    #endif

    /*
     * Gzip compression of $<Script-Name> (no \0 terminator in array)
     * Originally $<length of script-uncompressed> bytes
     */
    static const REBYTE script_compressed[$<length of script-compressed>] = {
        $<Binary-To-C Script-Compressed>
    };

    /*
     * Pointers to function dispatchers for natives (in same order as the
     * order of native specs after being loaded).
     */
    static REBNAT native_dispatchers[$<num-natives> + 1] = {
        $[Dispatcher_C_Names],
        nullptr /* just here to ensure > 0 length array (C++ requirement) */
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
    EXT_API REBVAL *RX_COLLATE_NAME(${Mod})(RL_LIB *api) {
      #ifdef REB_EXT
        /* only DLLs need to call rebXXX() APIs through a table */
        /* built-in extensions can call the RL_rebXXX() forms directly */
        RL = api;
      #else
        UNUSED(api);
      #endif

        return rebCollateExtension_internal(
            script_compressed,  /* script compressed data */
            sizeof(script_compressed),  /* size of script compressed data */
            $<script-num-codepoints>,  /* codepoints in uncompressed utf8 */
            native_dispatchers,  /* C function pointers for native bodies */
            $<num-natives>  /* number of NATIVE invocations in script */
        );
    }
}

e/write-emitted
