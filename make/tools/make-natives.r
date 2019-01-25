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

do %bootstrap-shim.r
do %common.r
do %common-parsers.r
do %native-emitters.r ;for emit-native-proto

print "------ Generate tmp-natives.r"

r3: system/version > 2.100.0

src-dir: %../../src
output-dir: system/options/path/prep
mkdir/deep output-dir/boot

verbose: false

unsorted-buffer: make text! 20000

process: func [
    file
    ; <with> the-file ;-- note external variable (can't do this in R3-Alpha)
][
    the-file: file
    if verbose [probe [file]]

    source.text: read join src-dir/core/% file
    if r3 [source.text: deline to-text source.text]
    proto-parser/emit-proto: :emit-native-proto
    proto-parser/process source.text
]

;-------------------------------------------------------------------------

output-buffer: make text! 20000


proto-count: 0

files: sort read src-dir/core/%

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


print "------ Generate tmp-generics.r"

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

boot-types: load src-dir/boot/types.r

append output-buffer mold/only load src-dir/boot/generics.r

append output-buffer unspaced [
    newline
    "_ ;-- C code expects BLANK! evaluation result, at present" newline
    newline
]

write-if-changed output-dir/boot/tmp-generics.r output-buffer
