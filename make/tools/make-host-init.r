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

do %bootstrap-shim.r
do %common.r
do %common-emitter.r

; This script starts running in the %make/ directory, but the %host-main.c
; file which wants to #include "tmp-host-start.inc" currently lives in the
; %os/ directory.  (That's also where host-start.r is.)
;
change-dir %../../src/os

args: parse-args system/options/args
output-dir: system/options/path/prep
mkdir/deep output-dir/os

print "--- Make Host Init Code ---"

write-c-file: function [
    return: <void>
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

    compressed: gzip data

    e/emit 'compressed {
        /*
         * Gzip compression of host initialization code
         * Originally $<length of data> bytes
         */
        #define REB_INIT_SIZE $<length of compressed>
        const unsigned char Reb_Init_Code[REB_INIT_SIZE] = {
            $<Binary-To-C Compressed>
        };
    }

    e/write-emitted
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

host-code: load-files [
    %encap.reb
    %unzip.reb
    %host-start.r
]

; `do host-code` evaluates to the HOST-START function, so it is easily found
; as the result of running the code in %host-main.c
;
append host-code [:host-start]

file-base: has load %../../make/tools/file-base.r

; copied from make-boot.r
host-protocols: make block! 2
for-each file file-base/prot-files [
    m: load/all join %../mezz/ file
    assert ['REBOL = m/1]
    spec: ensure block! m/2
    contents: skip m 2
    append host-protocols compose/only [(spec) (contents)]
]

insert host-code compose/only [host-prot: (host-protocols)]

write-c-file output-dir/os/tmp-host-start.inc host-code
