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
src: fix-win32-path to file! :args/SRC
set [in-dir file-name] split-path src
output-dir: fix-win32-path to file! any [:args/OUTDIR in-dir]
replace output-dir "../src" "prep"
mkdir/deep output-dir

dest: either select args 'DEST [
    fix-win32-path to file! :args/DEST
][
    join-of output-dir either ext-name: any [
        find/last file-name ".reb"
        find/last file-name ".r3"
        find/last file-name ".r"
    ][
        join-of %tmp- head change ext-name ".inc"
    ][
        fail ["ext-name has to be one of [reb r3 r]:" file-name]
    ]
]

print unspaced ["--- Make Extension Init Code from " src " ---"]

write-c-file: function [
    c-file
    r-file
][
    e: make-emitter "Ext custom init code" c-file

    data: read r-file

    comp-data: compress data
    comp-size: length-of comp-data

    e/emit-line ["static const REBYTE script_bytes[" comp-size "] = {"]

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

write-c-file dest src
