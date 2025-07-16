Rebol [
    title: "Test-framework"
    file: %test-framework.r
    copyright: [2012 "Saphirion AG"]
    license: {
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    }
    author: "Ladislav Mecir"
    purpose: "Test framework"
]

do %test-parsing.r

make object! compose [
    log-file: null

    log: func [report [block!]] [
        write/append log-file unspaced report
    ]

    ; counters
    skipped: null
    test-failures: null
    crashes: null
    dialect-failures: null
    successes: null

    allowed-flags: null

    process-vector: function [
        return: [~]
        flags [block!]
        source [text!]
        <with> test-failures successes skipped
    ][
        log [source]

        if not empty? exclude flags allowed-flags [
            skipped: me + 1
            log [space "skipped" newline]
            return ~
        ]

        case [
            error? sys/util/rescue [test-block: as block! load source] [
                "cannot load test source"
            ]

            (
                print mold test-block ;-- !!! make this an option

                set the result: sys/util/enrescue test-block
                recycle
                null
            )[
                ; Poor-man's elide (clause that evaluates to null)
            ]

            error? :result [
                any [
                    to text! opt result/id
                    mold result/message   ; errors with no ID may have BLOCK!
                    "(unknown)"
                ]
            ]


            (
                result: unlift result
                null
            )[
                ; Poor-man's elide (clause that evaluates to null)
            ]

            null? :result [
                "test returned null"
            ]
            not logic? :result [
                spaced ["was" (an type of :result) ", not logic!"]
            ]
            not :result [
                "test returned ~null~ antiform"
            ]
        ] then message -> [
            test-failures: me + 1
            log reduce [space {"failed, } message {"} newline]
        ] else [
            successes: me + 1
            log reduce [space {"succeeded"} newline]
        ]
    ]

    total-tests: 0

    process-tests: function [
        return: [~]
        test-sources [block!]
        emit-test [action!]
    ][
        parse test-sources [
            opt some [
                flags: block! value: one (
                    emit-test flags to text! value
                )
                    |
                value: file! (log ["^/" mold value "^/^/"])
                    |
                'dialect value: text! (
                    log [value]
                    set 'dialect-failures (dialect-failures + 1)
                )
            ]
        ]
    ]

    set 'do-recover func [
        {Executes tests in the FILE and recovers from crash}
        return: [block!]
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
                print ["=== NEW LOG:" log-file "==="]
                process-tests test-sources :process-vector
            ]

            all [
                elide print ["=== READING OLD LOG:" log-file "==="]

                parse/match read log-file [
                    (
                        last-vector: null
                        guard: [<end> one]
                    )
                    opt some [
                        opt some whitespace
                        [
                            position: "%" (
                                next-position: transcode/next3 position 'value
                            )
                            seek next-position
                                |
                            ; dialect failure?
                            some whitespace
                            {"} thru {"}
                            (dialect-failures: dialect-failures + 1)
                                |
                            last-vector: across ["(" test-source-rule ")"]
                            opt some whitespace
                            [
                                <end> (
                                    ; crash found
                                    crashes: crashes + 1
                                    log [{ "crashed"^/}]
                                    guard: null
                                )
                                    |
                                {"} value: across to {"} one
                                ; test result found
                                (
                                    parse value [
                                        "succeeded"
                                        (successes: me + 1)
                                            |
                                        "failed"
                                        (test-failures: me + 1)
                                            |
                                        "crashed"
                                        (crashes: me + 1)
                                            |
                                        "skipped"
                                        (skipped: me + 1)
                                            |
                                        (panic "invalid test result")
                                    ]
                                )
                            ]
                                |
                            "system/version:"
                            to <end>
                            (last-vector: guard: null)

                        ] position: <seek> guard break
                            |
                        seek position
                    ]
                ] else [
                    panic "do-recover log file parsing problem"
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

            return reduce [log-file summary]
        ] else [
            return reduce [log-file "testing already complete"]
        ]
    ]
]
