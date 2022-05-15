Rebol [
    Title: "Run-tests"
    File: %run-tests.r
    Copyright: [
        2014 "Ladislav Mecir and Saphirion AG"
        2014/2021 "Ren-C Open Source Contributors"
    ]
    License: {
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This script will either run a single file of tests, or if given a
        directory run all the tests in that directory.
    }
]

import %test-framework.r


=== TEMPORARILY MAKE DID AS USED IN TEST THE ULTIMATE FORM ===

; We want to allow DID ALL and DID ANY which are common in the tests.
; But right now they're choking on LOGIC! just to help find problems.
; Use the awkwardly-named THEN for the moment on these.

lib.did: :then?


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

log-file-prefix: join %r collect [
    for i length of version: system.version [
        keep "_"
        keep version.(i)
    ]
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
