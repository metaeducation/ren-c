REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate auto headers"
    File: %make-headers.r ;-- used by EMIT-HEADER to indicate emitting script
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
do %form-header.r
do %native-emitters.r ;for emit-include-params-macro and emit-native-include-params-macro

print "------ Building headers"
args: parse-args system/options/args
output-dir: fix-win32-path to file! any [args/OUTDIR %../]
mkdir/deep output-dir/include
mkdir/deep output-dir/core

r3: system/version > 2.100.0

verbose: false
check-duplicates: true
prototypes: make block! 10000 ; get pick [map! hash!] r3 1000
has-duplicates: false

change-dir %../core/

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
            emit-line ["RL_API " proto "; // " the-file]
            either "REBTYPE" = proto-parser/proto.id [
               emit-fsymb ["    SYM_FUNC(T_" proto-parser/proto.arg.1 "), // " the-file]
            ][
               emit-fsymb ["    SYM_FUNC(" proto-parser/proto.id "), // " the-file]
            ]
        ]
        proto-count: proto-count + 1
    ]
]

emit-directive: procedure [directive] [
    process-conditional directive proto-parser/parse.position :emit-line buf-emit
    process-conditional directive proto-parser/parse.position :emit-fsymb fsymbol-buffer
]

process-conditional: procedure [
    directive
    dir-position
    emit [function!]
    buffer
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
    true ;; func must return value
]

process: func [file] [
    if verbose [probe [file]]
    data: read the-file: file
    if r3 [data: deline to-string data]
    proto-parser/emit-proto: :emit-proto
    proto-parser/emit-directive: :emit-directive
    proto-parser/process data
]

;-------------------------------------------------------------------------

rlib: form-header/gen "REBOL Interface Library" %reb-lib.h %make-headers.r
append rlib newline
emit-rlib: func [d] [append adjoin rlib d newline]


;-------------------------------------------------------------------------

proto-count: 0

fsymbol-file: %tmp-symbols.c
fsymbol-buffer: make string! 20000
emit-fsymb: func [x] [append adjoin fsymbol-buffer x newline]

emit-header "Function Prototypes" %funcs.h

emit-fsymb form-header/gen "Function Symbols" fsymbol-file %make-headers.r
emit-fsymb {#include "sys-core.h"

// Note that cast() macro causes problems here with clang for some reason.
//
// !!! Also, void pointers and function pointers are not guaranteed to be
// the same size, even if TCC assumes so for these symbol purposes.
//
#define SYM_FUNC(x) {#x, cast(CFUNC*, x)}
#define SYM_DATA(x) {#x, &x}

struct rebol_sym_func_t {
    const char *name;
    CFUNC *func;
};

struct rebol_sym_data_t {
    const char *name;
    void *data;
};

extern const struct rebol_sym_func_t rebol_sym_funcs [];
const struct rebol_sym_func_t rebol_sym_funcs [] = ^{}

emit {
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
emit newline

boot-natives: load output-dir/boot/tmp-natives.r

for-each val boot-natives [
    if set-word? val [
        emit-line ["REBNATIVE(" to-c-name (to word! val) ");"]
    ]
]

emit {

//
// Other Prototypes: These are the functions that are scanned for in the %.c
// files by %make-headers.r, and then their prototypes placed here.  This
// means it is not necessary to manually keep them in sync to make calls to
// functions living in different sources.  (`static` functions are skipped
// by the scan.)
//
}
emit newline

file-base: has load %../tools/file-base.r

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


emit newline
emit-line "#ifdef __cplusplus"
emit-line "}"
emit-line "#endif"

write-emitted output-dir/include/tmp-funcs.h

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

emit-fsymb "^/    {NULL, NULL} //Terminator^/};"
emit-fsymb "^/// Globals from sys-globals.h^/"
emit-fsymb {
extern const struct rebol_sym_data_t rebol_sym_data [];
const struct rebol_sym_data_t rebol_sym_data [] = ^{^/}

the-file: %sys-globals.h
sys-globals.parser/process read/string %../include/sys-globals.h

emit-fsymb "^/    {NULL, NULL} //Terminator^/};"
write output-dir/core/:fsymbol-file fsymbol-buffer

;-------------------------------------------------------------------------

emit-header "PARAM() and REFINE() Automatic Macros" %func-args.h

action-list: load output-dir/boot/tmp-actions.r

; Search file for definition.  Will be `action-name: action [paramlist]`
;
for-next action-list [
    if 'action = action-list/2 [
        assert [set-word? action-list/1]
        emit-include-params-macro (to-word action-list/1) (action-list/3)
        emit newline
    ]
]

native-list: load output-dir/boot/tmp-natives.r

emit-native-include-params-macro native-list

write-emitted output-dir/include/tmp-paramlists.h


;-------------------------------------------------------------------------

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
            emit-line constd
        )
    ]
]

write-emitted output-dir/include/tmp-strings.h

if any [has-duplicates verbose] [
    print "** NOTE ABOVE PROBLEM!"
    wait 5
]

print "   "
