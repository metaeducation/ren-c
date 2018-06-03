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

    allowed-flags: _

    process-vector: function [
        return: <void>
        flags [block!]
        source [text!]
        <with> test-failures successes skipped
    ][
        log [source]

        if not empty? exclude flags allowed-flags [
            skipped: me + 1
            log [space "skipped" newline]
            return
        ]

        case [
            error? trap [test-block: make block! load source] [
                "cannot load test source"
            ]

            elide (
                print mold test-block ;-- !!! make this an option

                set* quote result: entrap test-block
                recycle
            )

            null? :result [
                "test returned null"
            ]
            error? :result [
                to string! result/id
            ]

            elide (result: first result)

            not logic? :result [
                spaced ["was" (an type of :result) ", not logic!"]
            ]
            not :result [
                "test returned #[false]"
            ]
        ] then message => [
            test-failures: me + 1
            log reduce [space {"failed, } message {"} newline]
        ] else [
            successes: me + 1
            log reduce [space {"succeeded"} newline]
        ]
    ]

    total-tests: 0

    process-tests: function [
        return: <void>
        test-sources [block!]
        emit-test [action!]
    ][
        parse test-sources [
            any [
                set flags: block! set value: skip (
                    emit-test flags to text! value
                )
                    |
                set value: file! (log ["^/" mold value "^/^/"])
                    |
                'dialect set value: text! (
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
                            copy last-vector ["(" test-source-rule ")"]
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
                                        (fail "invalid test result")
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
                    end | (fail "do-recover log file parsing problem")
                ]
                last-vector
                test-sources: try find/last/tail test-sources last-vector
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
                "system/version:" system/version LF
                "code-checksum:" code-checksum LF
                "test-checksum:" test-checksum LF
                "Total:" (
                    successes
                    + test-failures
                    + crashes
                    + dialect-failures
                    + skipped
                ) LF
                "Succeeded:" successes LF
                "Test-failures:" test-failures LF
                "Crashes:" crashes LF
                "Dialect-failures:" dialect-failures LF
                "Skipped:" skipped LF
            ]

            log [summary]

            reduce [log-file summary]
        ] else [
            reduce [log-file "testing already complete"]
        ]
    ]
]
