REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate auto headers"
    File: %make-headers.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

do %r2r3-future.r
do %common.r
do %common-emitter.r
do %common-parsers.r
do %native-emitters.r ; for emit-include-params-macro
file-base: has load %file-base.r

tools-dir: system/options/current-path
output-dir: system/options/path/prep
mkdir/deep output-dir/include

mkdir/deep output-dir/include
mkdir/deep output-dir/core

change-dir %../../src/core/

print "------ Building headers"

e-funcs: make-emitter "Function Prototypes" output-dir/include/tmp-funcs.h

e-syms: make-emitter "Function Symbols" output-dir/core/tmp-symbols.c

prototypes: make block! 10000 ; MAP! is buggy in R3-Alpha

emit-proto: proc [proto] [
    if any [
        find proto "static"
        find proto "REBNATIVE(" ; Natives handled by make-natives.r

        ; The REBTYPE macro actually is expanded in the tmp-funcs
        ; Should we allow macro expansion or do the REBTYPE another way?
        (comment [not find proto "REBTYPE("] false)
    ][
        leave
    ]

    header: proto-parser/data

    if not all [
        block? header 
        2 <= length-of header
        set-word? header/1
    ][
        print mold proto-parser/data
        fail [
            proto
            newline
            "Prototype has bad Rebol function header block in comment"
        ]
    ]

    switch/default header/2 [
        RL_API [
            ; Currently the RL_API entries should only occur in %a-lib.c, and
            ; are processed by %make-reb-lib.r.  Their RL_XxxYyy() forms don't
            ; appear in the %tmp-funcs.h file, but core includes %reb-lib.h
            ; and considers itself to have "non-extension linkage" to the API,
            ; so the calls can be directly linked without a struct.
            ;
            leave
        ]
        C [
            ; The only accepted type for now
        ]
        ; Natives handled by searching for REBNATIVE() currently.  If it
        ; checked for the word NATIVE it would also have to look for paths
        ; like NATIVE/BODY
    ][
        fail "%make-headers.r only understands C functions"
    ]

    if find prototypes proto [
        fail ["Duplicate prototype:" the-file ":" proto]
    ]

    append prototypes proto

    e-funcs/emit-line ["RL_API " proto "; // " the-file]
    either "REBTYPE" = proto-parser/proto.id [
        e-syms/emit-line [
            "    SYM_CFUNC(T_" proto-parser/proto.arg.1 "), // " the-file
        ]
    ][
        e-syms/emit-line [
            "    SYM_CFUNC(" proto-parser/proto.id "), // " the-file
        ]
    ]
]

process-conditional: procedure [
    directive
    dir-position
    emitter [object!]
][
    emitter/emit-line [
        directive
        ;;; " // " the-file " #" text-line-of dir-position
    ]

    ; Minimise conditionals for the reader - unnecessary for compilation.
    ;
    ; !!! Note this reaches into the emitter and modifies the buffer.
    ;
    if all [
        find/match directive "#endif"
        position: find/last tail-of emitter/buf-emit "#if"
    ][
        rewrite-if-directives position
    ]
]

emit-directive: procedure [directive] [
    process-conditional directive proto-parser/parse.position e-funcs
    process-conditional directive proto-parser/parse.position e-syms
]

process: function [
    file
    <with> the-file ;-- global we set
][
    ; !!! is DELINE necessary?
    data: deline to-string read the-file: file

    proto-parser/emit-proto: :emit-proto
    proto-parser/emit-directive: :emit-directive
    proto-parser/process data
]

;-------------------------------------------------------------------------

e-syms/emit {#include "sys-core.h"

// Note that cast() macro causes problems here with clang for some reason.
//
// !!! Also, void pointers and function pointers are not guaranteed to be
// the same size, even if TCC assumes so for these symbol purposes.
//
#define SYM_CFUNC(x) {#x, (CFUNC*)(x)}
#define SYM_DATA(x) {#x, &x}

struct rebol_sym_cfunc_t {
    const char *name;
    CFUNC *cfunc;
};

struct rebol_sym_data_t {
    const char *name;
    void *data;
};

extern const struct rebol_sym_cfunc_t rebol_sym_cfuncs [];
const struct rebol_sym_cfunc_t rebol_sym_cfuncs [] = ^{
}

e-funcs/emit {
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
//     if (VAL_ACT_DISPATCHER(native) == &N_parse) { ... }
//
}
e-funcs/emit newline

boot-natives: load output-dir/boot/tmp-natives.r

for-each val boot-natives [
    if set-word? val [
        e-funcs/emit-line ["REBNATIVE(" to-c-name (to word! val) ");"]
    ]
]

e-funcs/emit {

//
// Other Prototypes: These are the functions that are scanned for in the %.c
// files by %make-headers.r, and then their prototypes placed here.  This
// means it is not necessary to manually keep them in sync to make calls to
// functions living in different sources.  (`static` functions are skipped
// by the scan.)
//
}
e-funcs/emit newline

for-each item file-base/core [
    ;
    ; Items can be blocks if there's special flags for the file (
    ; <no-make-header> marks it to be skipped by this script)
    ;
    either block? item [
        either all [
            2 <= length-of item
            <no-make-header> = item/2
        ][; skip this file
            continue
        ][
            file: to file first item
        ]
    ][
        file: to file! item
    ]

    assert [
        | %.c = suffix? file
        | not find/match file "host-"
        | not find/match file "os-"
    ]

    process file
]


e-funcs/emit newline
e-funcs/emit-line "#ifdef __cplusplus"
e-funcs/emit-line "}"
e-funcs/emit-line "#endif"

e-funcs/write-emitted

print [length-of prototypes "function prototypes"]
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
                e-syms/emit-line ["    SYM_DATA(" id "),"]
            )
        ]

        directive: [
            copy data [
                ["#ifndef" | "#ifdef" | "#if" | "#else" | "#elif" | "#endif"]
                any [not newline c-pp-token]
            ] eol
            (
                process-conditional data parse.position e-syms
            )
        ]

        other-segment: [thru newline]

    ] c.lexical/grammar

]

e-syms/emit "^/    {NULL, NULL} //Terminator^/};"
e-syms/emit "^/// Globals from sys-globals.h^/"
e-syms/emit {
extern const struct rebol_sym_data_t rebol_sym_data [];
const struct rebol_sym_data_t rebol_sym_data [] = ^{^/}

the-file: %sys-globals.h
sys-globals.parser/process read/string %../include/sys-globals.h

e-syms/emit "^/    {NULL, NULL} //Terminator^/};"
e-syms/emit newline

e-syms/write-emitted

;-------------------------------------------------------------------------

e-params: (make-emitter
    "PARAM() and REFINE() Automatic Macros"
    output-dir/include/tmp-paramlists.h)

action-list: load output-dir/boot/tmp-actions.r

; Search file for definition.  Will be `action-name: action [paramlist]`
;
for-next action-list [
    if 'action = pick action-list 2 [
        assert [set-word? action-list/1]
        (emit-include-params-macro
            e-params (to-word action-list/1) (action-list/3))
        e-params/emit newline
    ]
]

native-list: load output-dir/boot/tmp-natives.r
for-next native-list [
    if tail? next native-list [break]

    if any [
        'native = native-list/2
        all [path? native-list/2 | 'native = first native-list/2]
    ][
        assert [set-word? native-list/1]
        (emit-include-params-macro
            e-params (to-word native-list/1) (native-list/3)
        )
        e-params/emit newline
    ]
]

e-params/write-emitted

;-------------------------------------------------------------------------

e-strings: (make-emitter
    "REBOL Constants Strings" output-dir/include/tmp-strings.h)

parse to string! read %a-constants.c [
    some [
        to "^/const"
        copy constd to "="
        (
            remove constd
            insert constd "extern "
            append trim/tail constd #";"

            e-strings/emit-line constd
        )
    ]
]

e-strings/write-emitted
