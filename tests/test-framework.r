Rebol [
    Title: "Test-framework"
    File: %test-framework.r
    Copyright: [2012 "Saphirion AG"]
    License: {
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Ladislav Mecir"
    Purpose: "Test framework"
]

do %test-parsing.r
do %catch-any.r

make object! compose [
    log-file: _

    log: func [report [block!]] [
        write/append log-file join-of #{} report
    ]

    ; counters
    skipped: _
    test-failures: _
    crashes: _
    dialect-failures: _
    successes: _

    exceptions: make object! [
        return: "return/exit out of the test code"
        error: "error was caused in the test code"
        break: "break or continue out of the test code"
        throw: "throw out of the test code"
        quit: "quit out of the test code"
    ]

    allowed-flags: _

    process-vector: procedure [
        flags [block!]
        source [string!]
    ][
        log [source]

        unless empty? exclude flags allowed-flags [
            set 'skipped (skipped + 1)
            log [{ "skipped"^/}]
            leave
        ]

        if error? try [test-block: load source] [
            set 'test-failures (test-failures + 1)
            log [{ "failed, cannot load test source"^/}]
            leave
        ]

        error? set* 'test-block catch-any test-block 'exception

        test-block: case [
            exception [spaced ["failed," exceptions/:exception]]
            not logic? :test-block ["failed, not a logic value"]
            test-block ["succeeded"]
        ] else [
            "failed"
        ]

        recycle

        either test-block = "succeeded" [
            set 'successes (successes + 1)
            log [{ "} test-block {"^/}]
        ][
            set 'test-failures (test-failures + 1)
            log reduce [{ "} test-block {"^/}]
        ]
    ]

    total-tests: 0

    process-tests: procedure [
        test-sources [block!]
        emit-test [function!]
    ][
        parse test-sources [
            any [
                set flags: block! set value: skip (
                    emit-test flags to string! value
                )
                    |
                set value: file! (log ["^/" mold value "^/^/"])
                    |
                'dialect set value: string! (
                    log [value]
                    set 'dialect-failures (dialect-failures + 1)
                )
            ]
        ]
    ]

    set 'do-recover func [
        {Executes tests in the FILE and recovers from crash}
        file [file!] {test file}
        flags [block!] {which flags to accept}
        code-checksum [binary! blank!]
        log-file-prefix [file!]
        /local interpreter last-vector value position next-position
        test-sources test-checksum guard
    ] [
        allowed-flags: flags

        ; calculate test checksum
        test-checksum: checksum/method read-binary file 'sha1

        log-file: log-file-prefix

        if code-checksum [
            append log-file "_"
            append log-file copy/part skip mold code-checksum 2 6
        ]

        append log-file "_"
        append log-file copy/part skip mold test-checksum 2 6

        append log-file ".log"
        log-file: clean-path log-file

        collect-tests test-sources: copy [] file

        successes: test-failures: crashes: dialect-failures: skipped: 0

        case [
            not exists? log-file [
                print "new log"
                process-tests test-sources :process-vector
            ]

            all [
                parse read log-file [
                    (
                        last-vector: _
                        guard: [end skip]
                    )
                    any [
                        any whitespace
                        [
                            position: "%" (
                                set [value next-position]
                                    transcode/next
                                    position
                            )
                            :next-position
                                |
                            ; dialect failure?
                            some whitespace
                            {"} thru {"}
                            (dialect-failures: dialect-failures + 1)
                                |
                            copy last-vector ["[" test-source-rule "]"]
                            any whitespace
                            [
                                end (
                                    ; crash found
                                    crashes: crashes + 1
                                    log [{ "crashed"^/}]
                                    guard: _
                                )
                                    |
                                {"} copy value to {"} skip
                                ; test result found
                                (
                                    parse value [
                                        "succeeded"
                                        (successes: successes + 1)
                                            |
                                        "failed"
                                        (test-failures: test-failures + 1)
                                            |
                                        "crashed"
                                        (crashes: crashes + 1)
                                            |
                                        "skipped"
                                        (skipped: skipped + 1)
                                            |
                                        (do make error! "invalid test result")
                                    ]
                                )
                            ]
                                |
                            "system/version:"
                            to end
                            (last-vector: guard: _)

                        ] position: guard break
                            |
                        :position
                    ]
                    end | (fail "log file parsing problem")
                ]
                last-vector
                test-sources: find/last/tail test-sources last-vector
            ][
                print [
                    "recovering at:"
                    (
                        successes
                        + test-failures
                        + crashes
                        + dialect-failures
                        + skipped
                    )
                ]
                process-tests test-sources :process-vector
            ]
        ] then [
            summary: spaced [
                    |
                "system/version:" system/version
                    |
                "code-checksum:" code-checksum
                    |
                "test-checksum:" test-checksum
                    |
                "Total:" (
                    successes
                    + test-failures
                    + crashes
                    + dialect-failures
                    + skipped
                )
                    |
                "Succeeded:" successes
                    |
                "Test-failures:" test-failures
                    |
                "Crashes:" crashes
                    |
                "Dialect-failures:" dialect-failures
                    |
                "Skipped:" skipped
                    |
            ]

            log [summary]

            reduce [log-file summary]
        ] else [
            reduce [log-file "testing already complete"]
        ]
    ]
]
