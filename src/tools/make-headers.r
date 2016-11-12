REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate auto headers"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Carl Sassenrath"
    Needs: 2.100.100
]

do %common.r
do %common-parsers.r

print "------ Building headers"
args: parse-args system/options/args
output-dir: fix-win32-path to file! any [args/OUTDIR %../]
mkdir/deep output-dir/include

r3: system/version > 2.100.0

verbose: false
check-duplicates: true
prototypes: make block! 10000 ; get pick [map! hash!] r3 1000
has-duplicates: false

do %form-header.r

change-dir %../core/

emit-out: func [d] [append repend output-buffer d newline]
emit-rlib: func [d] [append repend rlib d newline]
emit-header: func [t f] [emit-out form-header/gen t f %make-headers]
emit-fsymb: func [x] [append repend fsymbol-buffer x newline]

collapse-whitespace: [some [change some white-space #" " | skip]]
bind collapse-whitespace c.lexical/grammar

emit-proto: proc [proto] [

    if find proto "()" [
        print [
            proto
            newline
            {C-Style void arguments should be foo(void) and not foo()}
            newline
            http://stackoverflow.com/questions/693788/c-void-arguments
        ]
        fail "C++ no-arg prototype used instead of C style"
    ]

    ;?? proto
    assert [proto]
    if all [
        not find proto "static"
        not find proto "REBNATIVE("

        ; The REBTYPE macro actually is expanded in the tmp-funcs
        ; Should we allow macro expansion or do the REBTYPE another way?
        (comment [not find proto "REBTYPE("] true)

        find proto #"("
    ][

        parse proto collapse-whitespace
        proto: trim proto

        either all [
            check-duplicates
            find prototypes proto
        ][
            print ["Duplicate:" the-file ":" proto]
            has-duplicates: true
        ][
            append prototypes proto
        ]
        either find proto "RL_API" [
            emit-rlib ["extern " proto "; // " the-file]
        ][
            emit-out ["extern " proto "; // " the-file]
            either "REBTYPE" = proto-parser/proto.id [
               emit-fsymb ["    SYM_FUNC(" proto-parser/proto.arg.1 "), // " the-file]
            ][
               emit-fsymb ["    SYM_FUNC(" proto-parser/proto.id "), // " the-file]
            ]
        ]
        proto-count: proto-count + 1
    ]
]

emit-directive: func [
    directive
    /local position
][
    process-conditional directive proto-parser/parse.position :emit-out output-buffer
    process-conditional directive proto-parser/parse.position :emit-fsymb fsymbol-buffer
]

process-conditional: func [
    directive
    dir-position
    emit [function!]
    buffer
    /local position
][
    emit [
        directive
        ;;; " // " the-file " #" line-of head dir-position dir-position
    ]

    ; Minimise conditionals for the reader - unnecessary for compilation.
    if all [
        find/match directive "#endif"
        position: find/last tail buffer "#if"
    ][
        rewrite-if-directives position
    ]
]

process: func [file] [
    if verbose [probe [file]]
    data: read the-file: file
    if r3 [data: deline to-string data]
    proto-parser/emit-proto: :emit-proto
    proto-parser/process data
]

;-------------------------------------------------------------------------

rlib: form-header/gen "REBOL Interface Library" %reb-lib.h %make-headers.r
append rlib newline


;-------------------------------------------------------------------------

proto-count: 0
output-buffer: make string! 20000
fsymbol-buffer: make string! 20000
fsymbol-file: %tmp-symbols.c

emit-header "Function Prototypes" %funcs.h

emit-fsymb form-header/gen "Function Symbols" fsymbol-file %make-headers.r
emit-fsymb {#include "sys-core.h"

#define SYM_FUNC(x) #x, cast(void*, x)
#define SYM_DATA(x) #x, &x

const void *rebol_symbols [] = ^{}

emit-out {
// When building as C++, the linkage on these functions should be done without
// "name mangling" so that library clients will not notice a difference
// between a C++ build and a C build.
//
// http://stackoverflow.com/q/1041866/
//
#ifdef __cplusplus
extern "C" ^{
#endif

//
// Native Prototypes: REBNATIVE is a macro which will expand such that
// REBNATIVE(parse) will define a function named `N_parse`.  The prototypes
// are included in a system-wide header in order to allow recognizing a
// given native by identity in the C code, e.g.:
//
//     if (VAL_FUNC_DISPATCHER(native) == &N_parse) { ... }
//
}

boot-booters: load %../boot/booters.r
boot-natives: load output-dir/boot/tmp-natives.r

nats: append copy boot-booters boot-natives

for-each val nats [
    if set-word? val [
        emit-out rejoin ["REBNATIVE(" to-c-name (to word! val) ");"]
    ]
]

emit-out {

//
// Other Prototypes: These are the functions that are scanned for in the %.c
// files by %make-headers.r, and then their prototypes placed here.  This
// means it is not necessary to manually keep them in sync to make calls to
// functions living in different sources.  (`static` functions are skipped
// by the scan.)
//
}

files: sort read %./

;do
[
    remove find files %a-lib2.c
    print "Non-extended reb-lib version"
    wait 5
]

for-each file files [
    if all [
        %.c = suffix? file
        not find/match file "host-"
        not find/match file "os-"
    ][process file]
]

emit-out {
#ifdef __cplusplus
^}
#endif
}

write output-dir/include/tmp-funcs.h output-buffer

print [proto-count "function prototypes"]
;wait 1

;-------------------------------------------------------------------------

sys-globals.parser: context [

    emit-directive: _
    emit-identifier: _
    parse.position: _
    id: _

    process: func [text] [parse text grammar/rule]

    grammar: context bind [

        rule: [
            any [
                parse.position:
                segment
            ]
        ]

        segment: [
            (id: _)
            span-comment
            | line-comment any [newline line-comment] newline
            | opt wsp directive
            | declaration
            | other-segment
        ]

        declaration: [
            some [opt wsp [copy id identifier | not #";" punctuator] ] #";" thru newline (
                emit-fsymb ["    SYM_DATA(" id "),"]
            )
        ]

        directive: [
            copy data [
                ["#ifndef" | "#ifdef" | "#if" | "#else" | "#elif" | "#endif"]
                any [not newline c-pp-token]
            ] eol
            (
                process-conditional data parse.position :emit-fsymb fsymbol-buffer
            )
        ]

        other-segment: [thru newline]

    ] c.lexical/grammar

]

emit-fsymb "^/// Globals from sys-globals.h^/"
the-file: %sys-globals.h
sys-globals.parser/process read/string %../include/sys-globals.h

emit-fsymb "^/    NULL, NULL //Terminator^/};"
write output-dir/core/:fsymbol-file fsymbol-buffer

;-------------------------------------------------------------------------

clear output-buffer

emit-header "Function Argument Enums" %func-args.h

action-list: load output-dir/boot/tmp-actions.r

make-arg-enums: func [word] [
    ; Search file for definition:
    def: find action-list to-set-word word
    def: skip def 2
    args: copy []
    refs: copy []
    ; Gather arg words:
    for-each w first def [
        if all [any-word? w | not set-word? w] [
            append args uw: uppercase replace/all form to word! w #"-" #"_"
            if refinement? w [append refs uw  w: to word! w]
        ]
    ]

    uword: uppercase form word
    replace/all uword #"-" #"_"
    word: lowercase copy uword

    ; Argument numbers:
    emit-out ["enum act_" word "_arg {"]
    emit-out [tab "ARG_" uword "_0,"]
    for-each w args [emit-out [tab "ARG_" uword "_" w ","]]
    emit-out [tab "ARG_" uword "_MAX"]
    emit-out "};^/"

    ; Argument bitmask:
    n: 0
    emit-out ["enum act_" word "_mask {"]
    for-each w args [
        emit-out [tab "AM_" uword "_" w " = 1 << " n ","]
        n: n + 1
    ]
    emit-out [tab "AM_" uword "_MAX"]
    emit-out "};^/"

    repend output-buffer ["#define ALL_" uword "_REFS ("]
    for-each w refs [
        repend output-buffer ["AM_" uword "_" w "|"]
    ]
    remove back tail output-buffer
    append output-buffer ")^/^/"

    ;?? output-buffer halt
]

for-each word [
    copy
    find
    select
    insert
    trim
    open
    read
    write
] [make-arg-enums word]

action-list: load output-dir/boot/tmp-natives.r

for-each word [
    checksum
    request-file
] [make-arg-enums word]

;?? output-buffer
write output-dir/include/tmp-funcargs.h output-buffer


;-------------------------------------------------------------------------

clear output-buffer

emit-header "REBOL Constants Strings" %str-consts.h

data: to string! read %a-constants.c

parse data [
    some [
        to "^/const"
        copy constd to "="
        (
            remove constd
            ;replace constd "const" "extern"
            insert constd "extern "
            append trim/tail constd #";"
            emit-out constd
        )
    ]
]

write output-dir/include/tmp-strings.h output-buffer

if any [has-duplicates verbose] [
    print "** NOTE ABOVE PROBLEM!"
    wait 5
]

print "   "
