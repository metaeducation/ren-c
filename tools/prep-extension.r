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

do %import-shim.r
import %common.r
import %bootstrap-shim.r
import %common-emitter.r
import %systems.r

; The way that the processing code for extracting Rebol information out of
; C file comments is written is that the PROTO-PARSER has several callback
; functions that can be registered to receive each item it detects.

import %common-parsers.r
import %native-emitters.r ; for emit-include-params-macro


; !!! We put the modules .h files and the .inc file for the initialization
; code into the %prep/<name-of-extension> directory, which is added to the
; include path for the build of the extension

args: parse-args system/script/args  ; either from command line or DO/ARGS
src: to file! :args/SRC
set [in-dir file-name] split-path src
output-dir: make-file [(system/options/path) prep / (in-dir)]
insert src %../
mkdir/deep output-dir


config: config-system try get 'args/OS_ID

mod: ensure text! args/MODULE
m-name: mod
l-m-name: lowercase copy m-name
u-m-name: uppercase copy m-name

c-src: make-file [../ (as file! ensure text! args/SRC)]


=== {CALCULATE NAMES OF BUILD PRODUCTS} ===

; !!! This would be a good place to explain what the output goal of this
; script is.

print ["building" m-name "from" c-src]

script-name: copy c-src
parse script-name [
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
parse inc-name [
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

; We reuse the emitter that is used on processing natives in the core source.
; It will add the information to UNSORTED-BUFFER.  There's also a Rebol-style
; header embedded in the comments at the top of the C file, though it's not
; clear what kind of actionable information should be put there.

e1: (make-emitter "Module C Header File Preface"
    make-file [(output-dir) tmp-mod- (l-m-name) .h])

header-in-c-comments: _

source-text: read/string c-src

proto-parser/emit-fileheader: func [header] [header-in-c-comments: header]

c-natives: make block! 128
proto-parser/count: 0
proto-parser/unsorted-buffer: make block! 100
proto-parser/emit-proto: :emit-native-proto

proto-parser/file: c-src

proto-parser/process source-text


=== {EXTRACT NATIVE NAMES AS A LIST OF WORDS} ===

; The block the proto-parser gives back is a flat block of fixed-size records.
; For easier processing, extract just the list of native names converted to
; words along with the spec.

num-natives: 0
native-list: collect [
    for-each [
        file line export-word set-word enfix-word proto-block
    ] proto-parser/unsorted-buffer [
        keep ^(as word! set-word)
        keep ^(proto-block/2)
        num-natives: num-natives + 1
    ]
]


=== {MAKE TEXT FROM VALIDATED NATIVE SPECS} ===

; The proto-parser does some light validation of the native specification.
; There could be some extension-specific processing on the native spec block
; or checking of refinements here.

specs-uncompressed: make text! 10000

for-each [
    file line export-word set-word enfix-word proto-block
] proto-parser/unsorted-buffer [
    append specs-uncompressed spaced compose [
        ((if export-word ["export"])) (mold set-word)
            ((if enfix-word ["enfix"])) ((mold/only proto-block))
            newline newline
    ]
]


=== {EMIT THE INCLUDE_PARAMS_OF_XXX MACROS FOR THE EXTENSION NATIVES} ===

e1/emit {
    #include "sys-ext.h" /* for things like DECLARE_MODULE_INIT() */

    /*
    ** INCLUDE_PARAMS_OF MACROS: DEFINING PARAM(), REF(), ARG()
    */
}
e1/emit newline

for-each [name spec] native-list [
    emit-include-params-macro/ext e1 name spec u-m-name
    e1/emit newline
]


=== {FORWARD-DECLARE REBNATIVE DISPATCHER PROTOTYPES} ===

; We need to put all the C functions that implement the extension's native
; into an array.  But those functions live in the C file for the module.
; There have to be prototypes in our `.inc` file with the arrays, in order
; to get at the addresses of those functions.

dispatcher-forward-decls: collect [
    for-each [name spec] native-list [
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
        REBVAL *N_${MOD}_##n(REBFRM *frame_)

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
;        Type: 'Module
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

e: make-emitter "Ext custom init code" make-file [(output-dir) (inc-name)]

initscript-body: stripload/header script-name 'header  ; header will be TEXT!

script-uncompressed: unspaced [
    "Rebol" space "["  ; header has no brackets
        header newline   ; won't actually be indented (indents were stripped)
    "]" newline
    newline
    specs-uncompressed newline
    initscript-body
]
script-num-codepoints: length of script-uncompressed

write make-file [(output-dir) "script-uncompressed.r"] script-uncompressed

script-compressed: gzip script-uncompressed

dispatcher_c_names: collect [  ; must be in the order that NATIVE is called!
    for-each [name spec] native-list [
        keep cscape/with {N_${MOD}_${Name}} [mod name]
    ]
]

e/emit 'mod {
    #include "sys-core.h" /* !!! Could this just use "rebol.h"? */

    #include "tmp-mod-$<mod>.h" /* for REBNATIVE() forward decls */

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
    EXT_API REBVAL *RX_COLLATE_NAME(${Mod})(void) {
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
