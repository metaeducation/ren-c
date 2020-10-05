REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate extention native header files"
    File: %prep-extension.r  ; EMIT-HEADER uses to indicate emitting script
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2018 Ren-C Open Source Contributors
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

do %common.r
do %common-emitter.r
do %systems.r

; The way that the processing code for extracting Rebol information out of
; C file comments is written is that the PROTO-PARSER has several callback
; functions that can be registered to receive each item it detects.
;

do %common-parsers.r
do %native-emitters.r ; for emit-include-params-macro


; !!! We put the modules .h files and the .inc file for the initialization
; code into the %prep/<name-of-extension> directory, which is added to the
; include path for the build of the extension

args: parse-args system/script/args  ; either from command line or DO/ARGS
src: fix-win32-path to file! :args/SRC
set [in-dir file-name] split-path src
output-dir: make-file [(system/options/path) prep / (in-dir)]
insert src %../
mkdir/deep output-dir


config: config-system try get 'args/OS_ID

mod: ensure text! args/MODULE
m-name: mod
l-m-name: lowercase copy m-name
u-m-name: uppercase copy m-name

c-src: join %../ fix-win32-path to file! ensure text! args/SRC

print ["building" m-name "from" c-src]


e1: (make-emitter "Module C Header File Preface"
    ensure file! join-all [output-dir/tmp-mod- l-m-name %.h])


verbose: false

proto-count: 0
module-header: _

source-text: read/string c-src

; When the header information in the comments at the top of the file is
; seen, save it into a variable.
;

proto-parser/emit-fileheader: func [header] [module-header: header]

; Reuse the emitter that is used on processing natives in the core source.
; It will add the information to UNSORTED-BUFFER
;
c-natives: make block! 128
unsorted-buffer: make text! 20000
proto-parser/emit-proto: :emit-native-proto

the-file: c-src  ; global used for comments in the native emitter

proto-parser/process source-text


;
; At this point the natives will all be in the UNSORTED-BUFFER.
;

native-list: load unsorted-buffer
;print ["*** specs:" mold native-list]

natdef: make object! [
    export: _
    spec: _
    platforms: _
    name: _
]

; === PARSE NATIVES INTO NATIVE-DEFINITION OBJECTS, CHECKING FOR VALIDITY ===

native-defs: collect [
    native-rule: [
        (
            n-export: _
            n-spec: _
            n-platforms: _
            n-name: _
            n-spec: _
        )
        ['export (n-export: true) | (n-export: false)]
        set n-name set-word! copy n-spec [
            'native block!
                |
            'native/body 2 block!
        ]
        opt [quote platforms: set n-platforms block!]
        (
            keep make natdef compose/only [
                export: (n-export)
                name: lit (to word! n-name)
                spec: (n-spec)  ; includes NATIVE or NATIVE/BODY
                platforms: (try copy n-platforms)
            ]
        )
    ]

    parse native-list [any native-rule end] else [
        fail [
            "Malformed native found in extension specs" mold native-list
        ]
    ]
]

; === REBUILD NATIVE LIST JUST FOR WHAT APPLIES TO THIS PLATFORM ===

; !!! This re-creation of the native list is a little silly (e.g. turning
; export into a LOGIC! and back into the word EXPORT again, and turning
; the name from a SET-WORD! to a WORD! and back to a SET-WORD! again).  But
; it was how the initial extension mechanism was written.  Review.

clear native-list

num-natives: 0
for-each native native-defs [
    catch [
        if blank? native/platforms [
            throw true  ; no PLATFORM: in def means it's on all platforms
        ]
        for-each plat native/platforms [
            case [
                word? plat [; could be os-base or os-name
                    if find reduce [config/os-name config/os-base] plat [
                        throw true
                    ]
                ]
                path? plat [; os-base/os-name format
                    if plat = as path! reduce [config/os-base config/os-name][
                        throw true
                    ]
                ]
                fail ["Unrecognized platform spec:" mold plat]
            ]
        ]
        null  ; not needed in newer Ren-C (CATCH w/no throw is NULL)
    ] else [
        continue  ; not supported
    ] 

    num-natives: num-natives + 1

    if native/export [
        append native-list 'export

        ; !!! This used to add to an "export-list" in the header of a module
        ; whose source was entirely generated.  The idea was that there could
        ; also be some attached user-written Rebol code as a "script", that
        ; provided any usermode functions and definitions that were to ship
        ; inside of the extension.
        ;
        ; Now the "script" *is* the module definition, edited by the user.
        ; It would be technically possible to inject header information into
        ; that definition as part of the build process, to add to its already
        ; existing "Exports: []" section.
        ;
        ; But for the moment, the system uses "internal magic" to get the
        ; native specs paired up with their corresponding CFUNC pointers thatis
        ; are embedded in a C-style array inside of a HANDLE!.  Since that
        ; internal magic does not inject the specs into that user-written
        ; module, it goes ahead and pays attention to the EXPORT word as
        ; well, in the style proposed here:
        ;
        ; http://www.rebol.net/r3blogs/0300.html
        ;
        comment [
            append export-list to word! native/name
            ...
            compose/only [
                Module [
                    Name: ...
                    Exports: (export-list)
                ]
            ]
        ]
    ]
    append native-list reduce [to set-word! native/name]
    append native-list native/spec
]

;print ["specs:" mold native-list]

specs-compressed: gzip (specs-uncompressed: to-binary mold/only native-list)


names: collect [
    for-each item native-list [
        if set-word? item [
            item: to word! item
            keep cscape/with {N_${MOD}_${Item}} 'item
        ]
    ]
]

native-forward-decls: collect [
    for-each item native-list [
        if set-word? item [
            item: to word! item
            keep cscape/with {REBNATIVE(${Item})} 'item
        ]
    ]
]


e1/emit {
    #include "sys-ext.h" /* for things like DECLARE_MODULE_INIT() */

    /*
    ** INCLUDE_PARAMS_OF MACROS: DEFINING PARAM(), REF(), ARG()
    */
}
e1/emit newline

iterate native-list [
    if native-list/1 = 'export [native-list: next native-list]
    if tail? next native-list [break]
    any [
        'native = native-list/2
        (path? native-list/2) and ['native = first native-list/2]
    ] then [
        assert [set-word? native-list/1]
        (emit-include-params-macro/ext e1
            (to-word native-list/1) (native-list/3)
            u-m-name)
        e1/emit newline
    ]
]


e1/emit {
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
    $[Native-Forward-Decls];
}
e1/emit newline

e1/write-emitted


script-name: copy c-src
parse script-name [
    some [thru "/"]
    change "mod-" "ext-"
    to "."
    change "." "-init."
    change ["c" end | "cpp" end] "reb"
] else [
    fail [
        "Extension main file should have naming pattern %mod-xxx.c(pp),"
        "and Rebol initialization should be %ext-xxx-init.reb"
    ]  ; auto-generating version of initial (and poor) manual naming scheme
]


; === [{Make Extension Init Code from} script-name] ===

inc-name: second split-path c-src
is-cpp: false
parse inc-name [
    change "mod-" "tmp-mod-"
    to "."
    change "." "-init."
    change ["c" end | "cpp" end] "c"  ; !!! Keep as .cpp if it is?
] else [
    fail [
        "Extension main file should have naming pattern %mod-xxx.c(pp),"
        "so extension init code generates as %tmp-mod-xxx-init.c"
    ]  ; auto-generating version of initial (and poor) manual naming scheme
]

dest: join output-dir inc-name

if is-cpp [print [mold dest] wait 2]

e: make-emitter "Ext custom init code" dest

; Review: This does not use STRIPLOAD but encodes the script as C bytes
; verbatim--comments and whitespace and all.  That may be desirable if there
; is some way to view or extract the source.  It could also be a build option
; to use the STRIPLOAD function for further compression.
;
script-compressed: gzip (script-uncompressed: read script-name)

e/emit {
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
     * Gzip compression of native specs (no \0 terminator in array)
     * Originally $<length of specs-uncompressed> bytes
     */
    static const REBYTE specs_compressed[$<length of specs-compressed>] = {
        $<Binary-To-C Specs-Compressed>
    };

    /*
     * Pointers to function dispatchers for natives (in same order as the
     * order of native specs after being loaded).
     */
    static REBNAT native_dispatchers[$<num-natives> + 1] = {
        $[Names],
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
            script_compressed, sizeof(script_compressed),
            specs_compressed, sizeof(specs_compressed),
            native_dispatchers, $<num-natives>
        );
    }
}

e/write-emitted
