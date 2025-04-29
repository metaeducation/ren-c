Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Make REBOL host initialization code"
    file: %make-host-init.r
    rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    package: "REBOL 3 Host Kit"
    version: 1.1.1
    needs: 2.100.100
    purpose: {
        Build a single init-file from a collection of scripts.
        This is used during the REBOL host startup sequence.
    }
]

; **SENSITIVE MAGIC LINE OF VOODOO** - see "Usage" in %bootstrap-shim.r
(change-dir do join copy system/script/path %bootstrap-shim.r)

do <common.r>
do <common-emitter.r>

; Due to CHANGE-DIR above, this script starts in the directory where the user
; was when the interpreter was invoked.
;
; The %host-main.c file which wants to #include "tmp-host-start.inc" currently
; lives in the %os/ directory.  (That's also where host-start.r is.)
;
change-dir repo-dir
change-dir %src/os/

args: parse-args system/options/args
output-dir: system/options/path/prep
mkdir/deep output-dir/os

print "--- Make Host Init Code ---"

write-c-file: function [
    return: [~]
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

    e/emit [compressed {
        /*
         * Gzip compression of host initialization code
         * Originally $<length of data> bytes
         */
        #define REB_INIT_SIZE $<length of compressed>
        const unsigned char Reb_Init_Code[REB_INIT_SIZE] = {
            $<Binary-To-C Compressed>
        };
    }]

    e/write-emitted
]


load-files: function [
    return: [block!]
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
    return data
]

host-code: load-files [
    %unzip.reb
    %host-start.r
]

; `do host-code` evaluates to the HOST-START function, so it is easily found
; as the result of running the code in %host-main.c
;
append host-code [:host-start]

file-base: make object! load <file-base.r>

; copied from make-boot.r
host-protocols: make block! 2
for-each file file-base/prot-files [
    m: load/all join %../mezz/ file
    assert ['Rebol = m/1]
    spec: ensure block! m/2
    contents: skip m 2
    append host-protocols compose/only [(spec) (contents)]
]

insert host-code compose/only [host-prot: (host-protocols)]

write-c-file output-dir/os/tmp-host-start.inc host-code
