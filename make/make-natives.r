REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate native specifications"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "@codebybrett"
    Needs: 2.100.100
]

do %r2r3-future.r
do %common.r
do %common-parsers.r
do %native-emitters.r ;for emit-native-proto

print "------ Generate tmp-natives.r"

r3: system/version > 2.100.0

args: parse-args system/options/args
output-dir: fix-win32-path to file! any [:args/OUTDIR %prep/]
mkdir/deep output-dir/boot

verbose: false

unsorted-buffer: make string! 20000

process: func [
    file
    ; <with> the-file ;-- note external variable (can't do this in R3-Alpha)
][
    the-file: file
    if verbose [probe [file]]

    source.text: read join-of core-folder file
    if r3 [source.text: deline to-string source.text]
    proto-parser/emit-proto: :emit-native-proto
    proto-parser/process source.text
]

;-------------------------------------------------------------------------

output-buffer: make string! 20000


proto-count: 0

files: sort read core-folder: %../src/core/

remove-each file files [

    not all [
        %.c = suffix? file
        not find/match file "host-"
        not find/match file "os-"
    ]
]

for-each file files [process file]

append output-buffer unsorted-buffer

write-if-changed output-dir/boot/tmp-natives.r output-buffer

print [proto-count "natives"]
print " "


print "------ Generate tmp-actions.r"

clear output-buffer

append output-buffer {REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Action function specs"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {This is a generated file.}
]

}

boot-types: load %../src/boot/types.r

append output-buffer mold/only load %../src/boot/actions.r

append output-buffer unspaced [newline newline]

write-if-changed output-dir/boot/tmp-actions.r output-buffer
