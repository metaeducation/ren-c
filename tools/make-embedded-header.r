REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate C string for the embedded headers"
    File: %make-embedded-header.r
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

do %common.r
do %common-emitter.r
do %common-parsers.r

print "------ Building embedded header file"
args: parse-args system/script/args  ; either from command line or DO/ARGS
output-dir: system/options/path/prep
mkdir/deep make-file [(output-dir) core /]

inp: read fix-win32-path to file! output-dir/include/sys-core.i
replace/all inp "// #define" "#define"
replace/all inp "// #undef" "#undef"
replace/all inp "<ce>" "##" ;bug in tcc??

;remove "#define __BASE_FILE__" to avoid duplicates
remove-macro: func [
    return: <void>
    macro [any-string!]
    <local> pos-m inc eol
][
    macro: to binary! macro
    if pos-m: find inp macro [
        inc: find-reverse pos-m to binary! "#define"
        eol: find pos-m to binary! newline
        remove/part inc (index? eol) - (index? inc)
    ]
]

remove-macro "__BASE_FILE__"

;remove everything up to DEBUG_STDIO_OK
;they all seem to be builtin macros
remove/part inp -1 + index? find inp to binary! "#define DEBUG_STDIO_OK"

;write %/tmp/sys-core.i inp

e: (make-emitter
    "Embedded sys-core.h" output-dir/core/tmp-embedded-header.c)

e/emit {
    #include "sys-core.h"

    /*
     * !!! Typically embedded source in Rebol is compressed, and uses the
     * internal decompression to rebRepossess() a BINARY! from it.  This gives
     * an implicit 0x00 byte past the length, if any usages of that binary
     * ever need it (e.g. for a C string conversion).  But since this is not
     * compressed it doesn't get it for free.  Add manually, but review.
     */
    extern const REBYTE core_header_source[];
    const REBYTE core_header_source[] = {
        $<Binary-To-C Join Inp #{00}>
    };
}

print "------ Writing embedded header file"
e/write-emitted
