Rebol [
    Title: "Core tests run with crash recovery"
    File: %run-recover.r
    Copyright: [2012 "Saphirion AG"]
    License: {
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Ladislav Mecir"
    Purpose: "Core tests"
]

do %test-framework.r

; Example runner for the REBOL/Core tests which chooses
; appropriate flags depending on the interpreter version.

do-core-tests: procedure [] [
    ; Check if we run R3 or R2.
    flags: pick [
        [<64bit> <r3only> <r3>]
        [<32bit> <r2only>]
    ] not blank? in system 'catalog

    ; calculate interpreter checksum
    case [
        all [file? system/options/boot #"/" = first system/options/boot] [
            interpreter-checksum: checksum/method read-binary
                system/options/boot 'sha1
        ]
        string? system/script/args [
            interpreter-checksum: checksum/method read-binary
                local-to-file system/script/args 'sha1
        ]
    ] else [
        ; use system/build
        interpreter-checksum: checksum/method to binary!
            mold system/build 'sha1
    ]

    log-file-prefix: copy %r
    repeat i length of version: system/version [
        append log-file-prefix "_"
        append log-file-prefix mold version/:i
    ]

    print "Testing ..."
    result: do-recover %core-tests.r flags interpreter-checksum log-file-prefix
    set [log-file summary] result

    print ["Done, see the log file:" log-file]
    print summary
]

do-core-tests
