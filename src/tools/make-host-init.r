REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make REBOL host initialization code"
    File: %make-host-init.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Package: "REBOL 3 Host Kit"
    Version: 1.1.1
    Needs: 2.100.100
    Purpose: {
        Build a single init-file from a collection of scripts.
        This is used during the REBOL host startup sequence.
    }
]

do %r2r3-future.r
do %common.r
do %common-emitter.r

args: parse-args system/options/args
output-dir: fix-win32-path to file! any [args/OUTDIR %../]
mkdir/deep output-dir/os

print "--- Make Host Init Code ---"

do %form-header.r

; Output directory for temp files:
dir: %os/

; This script starts running in the %tools/ directory, but the %host-main.c
; file which wants to #include "tmp-host-start.inc" currently lives in the
; %os/ directory.  (That's also where host-start.r is.)
;
change-dir %../os/


write-c-file: function [
    c-file
    code
][
    emit-header "Host custom init code" c-file

    data: either system/version > 2.7.5 [
        mold/flat/only code ; crashes 2.7
    ][
        mold/only code
    ]
    append data newline ; BUG? why does MOLD not provide it?

    insert data reduce ["; Copyright REBOL Technologies " now newline]
    insert tail data make char! 0 ; zero termination required

    comp-data: compress data
    comp-size: length comp-data

    emit-line ["#define REB_INIT_SIZE" space comp-size]

    emit-line "const unsigned char Reb_Init_Code[REB_INIT_SIZE] = {"

    emit binary-to-c comp-data
    emit-line "};"

    write-emitted c-file

    ;-- Output stats:
    print [
        newline
        "Compressed" length data "to" comp-size "bytes:"
        to-integer (comp-size / (length data) * 100)
        "percent of original"
    ]

    return comp-size
]


load-files: function [
    file-list
][
    data: make block! 100
    for-each file file-list [
        print ["loading:" file]
        file: load/header file
        header: take file
        if header/type = 'module [
            file: compose/deep [
                import module
                [
                    title: (header/title)
                    version: (header/version)
                    name: (header/name)
                ][
                    (file)
                ]
            ]
            ;probe file/2
        ]
        append data file
    ]
    data
]

host-start: load-files [
    %host-repl.r
    %host-encap.r
    %host-start.r
]

; script evaluates to the startup function, which will in turn evaluate
; to either an exit status code or a REPL function.
;
append host-start [:host-start]


file-base: has load %../tools/file-base.r

; copied from make-boot.r
host-protocols: make block! 2
for-each file file-base/prot-files [
    m: load/all join-of %../mezz/ file ; not REBOL word
    append/only append/only host-protocols m/2 skip m 2
]

insert host-start compose/only [host-prot: (host-protocols)]

write-c-file output-dir/os/tmp-host-start.inc host-start
