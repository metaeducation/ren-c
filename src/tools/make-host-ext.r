REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Build REBOL 3.0 boot extension module"
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
    Purpose: {
        Collects host-kit extension modules and writes them out
        to a .h file in a compilable data format.
    }
]

print "--- Make Host Boot Extension ---"

do %r2r3-future.r
do %common.r

args: parse-args system/options/args
output-dir: fix-win32-path to file! any [:args/OUTDIR %../]
mkdir/deep output-dir/include

;-- Collect Sources ----------------------------------------------------------

collect-files: func [
    "Collect contents of several source files and return combined with header."
    files [block!]
    <local> source data header
][
    source: make block! 1000

    for-each file files [
        data: load/all file
        remove-each [a b] data [issue? a] ; commented sections
        unless block? header: find data 'rebol [
            print ["Missing header in:" file] halt
        ]
        unless empty? source [data: next next data] ; first one includes header
        append source data
    ]

    source
]

;-- Emit Functions -----------------------------------------------------------

out: make string! 10000
emit: func [d] [adjoin out d]

emit-cmt: func [text] [
    emit [
{/***********************************************************************
**
**  } text {
**
***********************************************************************/

}]
]

form-name: func [word] [
    uppercase replace/all replace/all to-string word #"-" #"_" #"?" #"Q"
]

emit-file: function [
    "Emit command enum and source script code."
    file [file!]
    source [block!]
][
    source: collect-files source

    title: select source/2 to-set-word 'title
    name: form select source/2 to-set-word 'name
    replace/all name "-" "_"
    prefix: uppercase copy name

    e: make-emitter title join-all [output-dir/include %/ file %.h]

    e/emit-line ["enum " name "_commands {"]

    ; Gather exported words if exports field is a block:
    words: make block! 100
    exported-words: make block! 100
    src: source
    while [src: find src set-word!] [
        if all [
            <no-export> != first back src
            find [command func function funct] src/2
        ][
            append exported-words to-word src/1
        ]
        if src/2 = 'command [append words to-word src/1]
        src: next src
    ]

    if block? exports: select second source to-set-word 'exports [
        insert exports exported-words
    ]

    for-each word words [
        e/emit-line [
            spaced-tab
            "CMD_" prefix #"_" replace/all form-name word "'" "_LIT"  ","
        ]
    ]
    e/emit-line [spaced-tab "CMD_MAX"]
    e/emit-line ["};"]
    e/emit-newline

    if src: select source to-set-word 'words [
        e/emit-line ["enum " name "_words {"]
        e/emit-line [spaced-tab "W_" prefix "_0,"]
        for-each word src [
            e/emit-line [spaced-tab "W_" prefix #"_" form-name word ","]
        ]
        e/emit-line [spaced-tab "W_MAX"]
        e/emit-line ["};"]
        e/emit newline
    ]

    e/emit ["#ifdef INCLUDE_EXT_DATA" newline]
    code: append trim/head mold/only/flat source newline
    append code to-char 0 ; null terminator may be required
    e/emit-lines [
        ["const unsigned char RX_" name "[] = {"]
        [binary-to-c to-binary code]
        "};"
    ]
    e/emit ["#endif" newline]

    e/write-emitted
]
