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

; This script starts running in the %make/ directory, but the %host-main.c
; file which wants to #include "tmp-host-start.inc" currently lives in the
; %os/ directory.  (That's also where host-start.r is.)
;
change-dir %../src/os

args: parse-args system/options/args
output-dir: fix-win32-path to file! any [:args/OUTDIR %../../make/prep/]
mkdir/deep output-dir/os

print "--- Make Host Init Code ---"

write-c-file: function [
    c-file
    code
][
    e: make-emitter "Host custom init code" c-file

    data: either system/version > 2.7.5 [
        mold/flat/only code ; crashes 2.7
    ][
        mold/only code
    ]
    append data newline ; BUG? why does MOLD not provide it?

    insert data reduce ["; Copyright REBOL Technologies " now newline]
    insert tail-of data make char! 0 ; zero termination required

    comp-data: compress data
    comp-size: length-of comp-data

    e/emit-line ["#define REB_INIT_SIZE" space comp-size]

    e/emit-line "const unsigned char Reb_Init_Code[REB_INIT_SIZE] = {"

    e/emit binary-to-c comp-data
    e/emit-line "};"

    e/write-emitted

    ;-- Output stats:
    print [
        newline
        "Compressed" length-of data "to" comp-size "bytes:"
        to-integer (comp-size / (length-of data) * 100)
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
    %encap.reb
    %unzip.reb
    %host-start.r
    %host-console.r
]

; script evaluates to the HOST-CONSOLE.  This is the userspace handler that is
; called in a loop.  By protocol it knows if it's being called for the first
; time, and hence is the generalized entry point.
;
append host-start [:host-console]

file-base: has load %../../make/file-base.r

; copied from make-boot.r
host-protocols: make block! 2
for-each file file-base/prot-files [
    m: load/all join-of %../mezz/ file ; not REBOL word
    append/only append/only host-protocols m/2 skip m 2
]

insert host-start compose/only [host-prot: (host-protocols)]

write-c-file output-dir/os/tmp-host-start.inc host-start
