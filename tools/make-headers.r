REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate auto headers"
    File: %make-headers.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

if not find words of :import [product] [  ; See %import-shim.r
    do load append copy system/script/path %import-shim.r
]

import <common.r>
import <common-emitter.r>
import <common-parsers.r>
import <native-emitters.r>  ; for EMIT-INCLUDES-PARAM-MACRO
import <text-lines.reb>

c-lexical: import <c-lexicals.r>

file-base: make object! load join repo-dir %tools/file-base.r

tools-dir: system/options/current-path
output-dir: join system/options/path %prep/
mkdir/deep (join output-dir %include/)

mkdir/deep (join output-dir %include/)
mkdir/deep (join output-dir %core/)

change-dir join repo-dir %src/core/

print "------ Building headers"

e-funcs: make-emitter "Internal API" (
    join output-dir %include/tmp-internals.h
)

prototypes: make block! 10000 ; MAP! is buggy in R3-Alpha

emit-proto: func [
    return: <none>
    proto
][
    any [
        find proto "static"
        find proto "REBNATIVE(" ; Natives handled by make-natives.r

        ; The REBTYPE macro actually is expanded in %tmp-internals.h
        ; Should we allow macro expansion or do the REBTYPE another way?
        ; `not find proto "REBTYPE("]`
    ] then [
        return none
    ]

    header: proto-parser/data

    all [
        block? header
        2 <= length of header
        set-word? header/1
    ] else [
        print mold proto-parser/data
        fail [
            proto
            newline
            "Prototype has bad Rebol function header block in comment"
        ]
    ]

    switch header/2 [
        'RL_API [
            ; Currently the RL_API entries should only occur in %a-lib.c, and
            ; are processed by %make-reb-lib.r.  Their RL_XxxYyy() forms are
            ; not in the %tmp-internals.h file, but core includes %rebol.h
            ; and considers itself to have "non-extension linkage" to the API,
            ; so the calls can be directly linked without a struct.
            ;
            return none
        ]
        'C [
            ; The only accepted type for now
        ]
        ; Natives handled by searching for REBNATIVE() currently.  If it
        ; checked for the word NATIVE it would also have to look for paths
        ; if natives took refinements (as they once took NATIVE/BODY)

        fail "%make-headers.r only understands C functions"
    ]

    if find prototypes proto [
        fail ["Duplicate prototype:" proto-parser/file ":" proto]
    ]

    append prototypes proto

    e-funcs/emit [proto proto-parser/file] {
        RL_API $<Proto>; /* $<proto-parser/file> */
    }
]

process-conditional: function [
    return: <none>
    directive
    dir-position
    emitter [object!]
][
    emitter/emit [proto-parser/file dir-position text-line-of directive] {
        $<Directive> /* $<proto-parser/file> #$<text-line-of dir-position> */
    }

    ; Minimise conditionals for the reader - unnecessary for compilation.
    ;
    ; !!! Note this reaches into the emitter and modifies the buffer.
    ;
    all [
        find/match directive "#endif"
        position: find-last tail-of emitter/buf-emit "#if"
    ] then [
        rewrite-if-directives position
    ]
]

emit-directive: function [return: <none> directive] [
    process-conditional directive proto-parser/parse-position e-funcs
]

process: function [return: <none> file] [
    proto-parser/emit-proto: :emit-proto
    proto-parser/file: file
    proto-parser/emit-directive: :emit-directive
    proto-parser/process (as text! read file)
]

;-------------------------------------------------------------------------

; !!! Note the #ifdef conditional handling here is weird, and is based on
; the emitter state.  So it would take work to turn this into something
; that would collect the symbols and then insert them into the emitter
; all at once.  The original code seems a bit improvised, and could use a
; more solid mechanism.


boot-natives: load (join output-dir %boot/tmp-natives.r)

e-funcs/emit {
    /*
     * When building as C++, the linkage on these functions should be done
     * without "name mangling" so that library clients will not notice a
     * difference between a C++ build and a C build.
     *
     * http://stackoverflow.com/q/1041866/
     */
    #ifdef __cplusplus
    extern "C" ^{
    #endif

    /*
     * NATIVE PROTOTYPES
     *
     * REBNATIVE is a macro which will expand such that REBNATIVE(parse) will
     * define a function named `N_parse`.  The prototypes are included in a
     * system-wide header in order to allow recognizing a given native by
     * identity in the C code, e.g.:
     *
     *     if (ACT_DISPATCHER(VAL_ACTION(native)) == &N_parse) { ... }
     */
}
e-funcs/emit newline

for-each val boot-natives [
    if set-word? val [
        e-funcs/emit 'val {
            REBNATIVE(${to word! val});
        }
    ]
]

e-funcs/emit {
    /*
     * OTHER PROTOTYPES
     *
     * These are the functions that are scanned for in the %.c files by
     * %make-headers.r, and then their prototypes placed here.  This means it
     * is not necessary to manually keep them in sync to make calls to
     * functions living in different sources.  (`static` functions are skipped
     * by the scan.)
     */
}
e-funcs/emit newline

for-each item file-base/core [
    ;
    ; Items can be blocks if there's special flags for the file (
    ; <no-make-header> marks it to be skipped by this script)
    ;
    if block? item [
        all [
            2 <= length of item
            <no-make-header> = item/2
        ] then [
            continue  ; skip this file
        ]

        file: to file! first item
    ] else [
        file: to file! item
    ]

    assert [
        %.c = suffix? file
        not find/match file "host-"
        not find/match file "os-"
    ]

    process file
]


e-funcs/emit {
    #ifdef __cplusplus
    ^}
    #endif
}

e-funcs/write-emitted

print [length of prototypes "function prototypes"]
;wait 1

;-------------------------------------------------------------------------

sys-globals-parser: context [

    emit-directive: _
    emit-identifier: _
    parse-position: ~
    id: _

    process: func [return: <none> text] [
        parse2 text grammar/rule  ; Review: no END (return result unused?)
    ]

    grammar: context bind [

        rule: [
            opt some [
                parse-position:  ; <here>
                segment
            ]
        ]

        segment: [
            (id: _)
            span-comment
            | line-comment opt some [newline line-comment] newline
            | opt wsp directive
            | declaration
            | other-segment
        ]

        declaration: [
            some [opt wsp [copy id identifier | not #";" punctuator] ] #";" thru newline (
                ;
                ; !!! Not used now, but previously was for user natives:
                ;
                ; https://forum.rebol.info/t/952/3
                ;
                ; Keeping the PARSE rule in case it's useful, but if it
                ; causes problems before it gets used again then it is
                ; probably okay to mothball it to that forum thread.
            )
        ]

        directive: [
            copy data [
                ["#ifndef" | "#ifdef" | "#if" | "#else" | "#elif" | "#endif"]
                opt some [not newline c-pp-token]
            ] eol
            (
                ; Here is where it would call processing of conditional data
                ; on the symbols.  This is how it would compensate for the
                ; preprocessor, so things that were #ifdef'd out would not
                ; make it into the list.
                ;
                comment [process-conditional data parse-position e-syms]
            )
        ]

        other-segment: [thru newline]

    ] c-lexical/grammar

]


sys-globals-parser/process read/string %../include/sys-globals.h

;-------------------------------------------------------------------------

e-strings: make-emitter "REBOL Constants with Global Linkage" (
    join output-dir %include/tmp-constants.h
)

e-strings/emit {
    /*
     * This file comes from scraping %a-constants.c for any `const XXX =` or
     * `#define` definitions, and it is included in %sys-core.h in order to
     * to conveniently make the global data available in other source files.
     */
}
for-each line read/lines %a-constants.c [
    case [
        did parse2 line ["#define" to end] [
            e-strings/emit line
            e-strings/emit newline
        ]
        did parse2 line [to {const } copy constd to { =} to end] [
            e-strings/emit 'constd {
                extern $<Constd>;
            }
        ]
    ]
]

e-strings/write-emitted
