REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate C string for the embedded headers"
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
do %common-parsers.r
do %form-header.r

print "------ Building embedded header file"
args: parse-args system/options/args
output-dir: fix-win32-path to file! any [:args/OUTDIR %../]
mkdir/deep output-dir/core

inp: read fix-win32-path to file! output-dir/include/sys-core.i
replace/all inp "// #define" "#define"
replace/all inp "// #undef" "#undef"
replace/all inp "<ce>" "##" ;bug in tcc??

;remove "#define __BASE_FILE__" to avoid duplicates
remove-macro: func [
    macro [any-string!]
    /local pos-m inc eol
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
out: make binary! 2048

append out form-header/gen "Embedded sys-core.h" %e-embedded-header.c %make-embedded-header.r

append out unspaced [
    {#include "sys-core.h"^/}
    "extern const REBYTE core_header_source[];^/"
    "const REBYTE core_header_source[] = {^/"
    binary-to-c join-of inp #{00}
    "};^/"
]

write output-dir/core/e-embedded-header.c  out
