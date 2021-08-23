Rebol [
    Title: "Run-tests"
    File: %run-tests.r
    Copyright: [2014 "Saphirion AG"]
    Author: "Ladislav Mecir"
    License: {
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Ladislav Mecir"
    Purpose: {Click and run tests in a file or directory}

    Description: {
        This script will either run a single file of tests, or if given a
        directory run all the tests in that directory.
    }
]

import %test-framework.r


=== SET UP FLAGS FOR WHICH TESTS TO RUN ===

flags: pick [
    [<64bit> <r3only> <r3>]
    [<32bit> <r2only>]
] not blank? in system 'catalog


=== CALCULATE INTERPRETER CHECKSUM ===

; The checksum of the interpreter running this script is used in the naming of
; log files.  This means tests from different interpreter versions won't
; overwrite each other, and incomplete test runs from a crashing interpreter
; can be detected in order to recover.

check: checksum 'sha1 to binary! mold system.build

log-file-prefix: copy %r
for i length of version: system.version [
    append log-file-prefix "_"
    append log-file-prefix mold version.(i)
]


=== TEST RUNNER FUNCTION ===

run-tests: func [file [file!]] [
    print ["=== Running Tests implied by" mold file "==="]

    [log-file summary]: do-recover tests flags check log-file-prefix

    print newline

    print "=== Summary ==="
    print summary

    print "=== Log-file ==="
    print mold log-file
    print newline
]


=== RUN TESTS SPECIFIED BY COMMAND LINE (SINGLE FILE OR DIRECTORY) ===

print newline

if first system.options.args [
    tests: local-to-file first system.options.args
] else [
    tests: %core-tests.r
]

if dir? tests [
    tests: dirize tests
    change-dir tests
    for-each file read tests [
        if %.test.reb = find-last file %.t [run-tests file]
    ]
] else [
    run-tests tests
]
