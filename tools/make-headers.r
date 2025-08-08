Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Generate auto headers"
    file: %make-headers.r
    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    needs: 2.100.100
    notes: --[
      * The Needful library introduces creative TypeMacros like Option(T).
        So we need special handling here to recognize that format.

        (see the `typemacro_parentheses` rule)
    ]--
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>
import <common-parsers.r>
import <native-emitters.r>  ; for EMIT-INCLUDES-PARAM-MACRO
import <text-lines.r>

c-lexical: import <c-lexicals.r>

file-base: make object! load3 join repo-dir %tools/file-base.r

tools-dir: system.options.current-path
output-dir: join system.options.path %prep/
mkdir:deep (join output-dir %include/)

mkdir:deep (join output-dir %include/)
mkdir:deep (join output-dir %core/)

change-dir join repo-dir %src/core/

print "------ Building headers"

e-funcs: make-emitter "Internal API" (
    join output-dir %include/tmp-internals.h
)

prototypes: make block! 10000 ; MAP! is buggy in R3-Alpha

emit-proto: func [
    return: []
    proto [text!]
][
    any [
        find proto "static"
        find proto "DECLARE_NATIVE(" ; Natives handled by make-natives.r

        ; should anything be done here with IMPLEMENT_GENERIC() ?
    ] then [
        return ~
    ]

    let header: proto-parser.data

    all [
        block? header
        2 <= length of header
        set-word? header.1
    ] else [
        print mold proto-parser.data
        panic [
            proto
            newline
            "Prototype has bad Rebol function header block in comment"
        ]
    ]

    switch header.2 [
        'API [
            ; Currently the API entries should only occur in %a-lib.c, and
            ; are processed by %make-librebol.r.  Their API_XxxYyy() forms are
            ; not in the %tmp-internals.h file, but core includes %rebol.h
            ; and considers itself to have "non-extension linkage" to the API,
            ; so the calls can be directly linked without a struct.
            ;
            return ~
        ]
        'C [
            ; The only accepted type for now
        ]
        ; Natives handled by searching for DECLARE_NATIVE() currently.  If it
        ; checked for the word NATIVE it would also have to look for paths
        ; if natives took refinements (as they once took NATIVE:BODY)

        print mold header
        panic "%make-headers.r only understands C functions"
    ]

    if find prototypes proto [
        panic ["Duplicate prototype:" proto-parser.file ":" proto]
    ]

    append prototypes proto

    e-funcs/emit [proto proto-parser.file --[
        RL_API $<Proto>; /* $<proto-parser.file> */
    ]--]
]

process-conditional: func [
    return: []
    directive
    dir-position
    emitter [object!]
][
    emitter/emit [proto-parser.file dir-position text-line-of directive --[
        $<Directive> /* $<proto-parser.file> #$<text-line-of dir-position> */
    ]--]

    ; Minimise conditionals for the reader - unnecessary for compilation.
    ;
    ; !!! Note this reaches into the emitter and modifies the buffer.
    ;
    all [
        find:match directive "#endif"
        let position: find-last (tail of emitter.buf-emit) "#if"
        elide rewrite-if-directives position
    ]
]

emit-directive: func [return: [] directive] [
    process-conditional directive proto-parser.parse-position e-funcs
]


;-------------------------------------------------------------------------

; !!! Note the #ifdef conditional handling here is weird, and is based on
; the emitter state.  So it would take work to turn this into something
; that would collect the symbols and then insert them into the emitter
; all at once.  The original code seems a bit improvised, and could use a
; more solid mechanism.


e-funcs/emit [--[
    /*
     * Once there was a rule that C++ builds would not be different in function
     * from a C build.  This way, an extension DLL could be compiled to run
     * in either, when compiled against %sys-core.h
     *
     * But libRebol has become good enough that it is likely going to be the
     * required interface for extensions, and the features enabled by C++ may
     * become too good to ignore.  So the internal API does permit the passing
     * of things like Utf8(*) and Strand* for now.
     *
     * This may be revisited in the future.
     */
    #if 0
    extern "C" {
    #endif
]--]

e-funcs/emit [--[
    /*
     * These are the functions that are scanned for in the %.c files by
     * %make-headers.r, and then their prototypes placed here.  This means it
     * is not necessary to manually keep them in sync to make calls to
     * functions living in different sources.  (`static` functions are skipped
     * by the scan.)
     */
]--]


; Items can be blocks if there's special flags for the file (<no-make-header>
; marks it to be skipped by this script)
;
handle-item: func [
    "Handle a single item in %file-base.r"
    return: []
    name [path! tuple! file!]  ; bootstrap EXE loads [foo.c] as [foo/c]
    dir [<opt> file!]
    options [<opt> block!]
][
    file: to file! name

    all [
        block? opt options
        find options <no-make-header>
    ] then [
        return ~  ; skip this file
    ]

    assert [
        %.c = suffix-of file
        not find:match file "host-"
        not find:match file "os-"
    ]

    proto-parser.emit-proto: emit-proto/
    proto-parser.file: to file! unspaced [opt subdir, file]
    proto-parser.emit-directive: emit-directive/
    proto-parser/process (as text! read proto-parser.file)
]

name: null
options: null
subdir: null
parse3 file-base.core [some [
    ahead [path! '->] subdir: path! '-> ahead block! into [  ; descend
        (subdir: to file! subdir)
        some [name: [tuple! | path! | file!] options: try block! (
            handle-item name subdir opt options
        )]
        (subdir: null)
    ]
    |
    name: [tuple! | path! | file!] options: try block! (
        handle-item name () opt options
    )
]]


e-funcs/emit --[
    #if 0
    }  /* end of `extern "C" {` */
    #endif
]--

e-funcs/write-emitted

print [length of prototypes "function prototypes"]


;-------------------------------------------------------------------------

sys-globals-parser: context [

    emit-directive: null
    emit-identifier: null
    parse-position: ~
    id: null
    data: ~

    process: func [return: [] text] [
        parse3 text grammar.rule  ; Review: no END (return result unused?)
    ]

    grammar: context bind:copy3 c-lexical.grammar [
        rule: [
            opt some [
                parse-position: <here>
                segment
            ]
        ]

        segment: [
            (id: null)
            span-comment
            | line-comment opt some [newline line-comment] newline
            | opt wsp directive
            | declaration
            | other-segment
        ]

        declaration: [
            some [
                opt wsp [id: across identifier | not ahead #";" punctuator]
            ] #";" thru newline (
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
            data: across [
                ["#ifndef" | "#ifdef" | "#if" | "#else" | "#elif" | "#endif"]
                opt some [not ahead newline c-pp-token]
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
    ]
]


sys-globals-parser/process read:string %../include/sys-globals.h

;-------------------------------------------------------------------------

e-strings: make-emitter "REBOL Constants with Global Linkage" (
    join output-dir %include/tmp-constants.h
)

e-strings/emit [--[
    /*
     * This file comes from scraping %a-constants.c for any `const XXX =` or
     * `#define` definitions, and it is included in %sys-core.h in order to
     * to conveniently make the global data available in other source files.
     */
]--]

for-each 'line read:lines %api/a-constants.c [
    let constd
    case [
        parse3:match line ["#define" to <end>] [
            e-strings/emit line
            e-strings/emit newline
        ]
        parse3:match line [constd: across to -[ =]- to <end>] [
            e-strings/emit [constd --[
                extern $<Constd>;
            ]--]
        ]
    ]
]

e-strings/write-emitted
