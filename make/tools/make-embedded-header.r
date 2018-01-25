REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate C string for the embedded headers"
    File: %make-embedded-header.r
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017 Rebol Open Source Contributors
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

print "------ Building embedded header file"
args: parse-args system/options/args
output-dir: system/options/path/prep
mkdir/deep output-dir/core

inp: read fix-win32-path to file! output-dir/include/sys-core.i
replace/all inp "// #define" "#define"
replace/all inp "// #undef" "#undef"
replace/all inp "<ce>" "##" ;bug in tcc??

;remove "#define __BASE_FILE__" to avoid duplicates
remove-macro: proc [
    macro [any-string!]
    <local> pos-m inc eol
][
    unless binary? macro [macro: to binary! macro]
    pos-m: find inp macro
    if pos-m [
        inc: find/reverse pos-m to binary! "#define"
        eol: find pos-m to binary! newline
        remove/part inc (index? eol) - (index? inc)
    ]
]

remove-macro "__BASE_FILE__"

;remove everything up to REN_C_STDIO_OK
;they all seem to be builtin macros
remove/part inp -1 + index? find inp to binary! "#define REN_C_STDIO_OK"

;write %/tmp/sys-core.i inp

e-embed: (make-emitter
    "Embedded sys-core.h" output-dir/core/tmp-embedded-header.c)

e-embed/emit-lines [
    {#include "sys-core.h"}
    "extern const REBYTE core_header_source[];"
    "const REBYTE core_header_source[] = {"
    [binary-to-c join-of inp #{00}]
    "};"
]

print "------ Writing embedded header file"
e-embed/write-emitted
