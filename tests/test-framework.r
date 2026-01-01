Rebol [
    title: "Test Framework"
    file: %test-framework.r
    type: module
    name: Test-Framework
    copyright: [
        2014 "Ladislav Mecir and Saphirion AG"
        2014/2021 "Ren-C Open Source Contributors"
    ]
    license: --[
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    ]--
]

import %test-parsing.r

[.]: construct [
    log-file: ~

    ; counters
    ;
    skipped: ~
    test-failures: ~
    crashes: ~
    dialect-failures: ~
    successes: ~

    allowed-flags: ~
]

; By default we echo the log to the screen also.  This should be an option.
;
log: proc [
    report [block!]
][
    write:append .log-file unspaced report
    write stdout unspaced report
]

run-single-test: proc [
    "Run code and write the success or failure to the log file"

    code "Code GROUP! from test file, assumed bound into isolated module"
        [group!]
    expected-id [<opt> word!]
][
    assert [expected-id <> '!!!]  ; dialecting mistake, vs. ???, easy to make

    log [mold code]

    let result: sys.util/enrecover code

    all [
        warning? result
        expected-id
        (all [not result.id, expected-id = '???]) or (result.id = expected-id)
    ] then [
        .successes: me + 1
        log reduce [_ -["correct failure:"]- _ @(quasi expected-id) newline]
        exit
    ]

    case [
        warning? result [
            spaced ["error" any [
                to text! opt result.id
                mold result.message   ; errors with no ID may have BLOCK!
                "(unknown)"
            ]]
        ]

        expected-id [
            spaced ["did not error, but expected:" @(quasi expected-id)]
        ]

        '~[]~ = result [
            "test returned empty pack ~[]~ antiform"  ; UNMETA panics
        ]
        (elide if pack? ^result [result: first unquasi result])

        result = '~okay~ [
            .successes: me + 1
            log reduce [_ -["succeeded"]- newline]
            exit
        ]

        result = '~null~ [
            "test returned null"
        ]

        quasi? result [
            "test returned antiform:" (mold:limit result 40)
        ]
        (elide result: unlift result)

        <default> [
            spaced ["was" (to word! type of result) ", not true or false"]
        ]
    ] then (message -> [
        .test-failures: me + 1
        log reduce [space -["failed, ]- message -["]- newline]
    ])
]

run-test-cluster: proc [
    flags [block!]
    cluster "Block of GROUP!s to be run together in a common isolated context"
        [block!]
][
    if not empty? exclude flags .allowed-flags [
        .skipped: me + 1
        log [space -["skipped"]- newline]
        exit
    ]

    ; Here we use MODULE instead of MAKE MODULE! so that we get IMPORT and
    ; INTERN available.  (MAKE MODULE! is lower level, and makes a completely
    ; empty context).  We pass a VOID instead of a header to indicate that
    ; it is a "do-style" context (this result also comes back from DO* as
    ; a secondary result, which could be useful as this feature expands.)
    ;
    ; We don't want the tests (represented with GROUP!) in the body block of
    ; the module, because we don't want to run them all in a batch.  But we
    ; could put service functions that tests could use there, e.g. such as
    ; specialized versions of assert.
    ;
    let isolate: module ^ghost inside lib '[
        print: lambda [x] [
            panic:blame "Don't use PRINT in tests" $x
        ]
    ]

    ; Code that begins a line and starts a GROUP! will be checked as a test.
    ; Everything else will be executed and its results discarded.  This
    ; permits sharing, cleanup, and opens many other possibilities.
    ;
    ; cluster, but ideally they would run in a module per-group that just
    ; *inherited* from the cluster.  Hopefully modules will be able to do
    ; things like that in the "near future"(tm).
    ;
    parse cluster [while not <end> [
        ;
        ; !!! Skip any RUNE! or URL!, as this style has been used:
        ;
        ;     [#123 http://example.com (
        ;        2 = 1 + 1
        ;     )]
        ;
        opt some [url! | rune!]

        let expected-id: (~)
        (expected-id: null)  ; default expects a true result, not error w/id

        opt [
            expected-id: quasiform! [(expected-id: unquasi expected-id)
                '!! ahead group!
                | (panic "~error-id~ must be followed by !! and a GROUP!")
            ]
        ]
        let group: (~)
        [
            group: group!
            | (panic "GROUP! expected in tests")
        ]
        (
            wrap* isolate group  ; gather top-level declarations
            run-single-test inside isolate group opt expected-id
        )
    ]]
]


; The tests are collected in a pre-phase with COLLECT-TESTS.  It produces a
; long list of BLOCK!s that are test groups.
;
process-tests: proc [
    test-sources [block!]
    handler [action!]
][
    let flags
    let value
    let test-file
    let body
    let collected
    parse3 test-sources [
        opt some [
            flags: block! value: block! (
                handler flags value  ; flags ignored atm
            )
                |
            test-file: file! (
                log ["^/" mold test-file "^/^/"]
                ;
                ; We'd like tests to be able to live anywhere on disk
                ; (e.g. extensions can have a %tests/ subdirectory).  If
                ; those tests have supplementary scripts or data files,
                ; the test should be able to refer to them via paths
                ; relative the directory where the test is running.  So
                ; we CHANGE-DIR to the test file's path.
                ;
                change-dir split-path test-file
            )
                |
            'dialect value: text! (  ; bad parse of test file itself
                log [value]
                .dialect-failures: me + 1
            )
                |
            'collect-tests body: block! (
                log ["@collect-tests" space mold body]

                let [_ collected]: module ^ghost compose:deep [collect [
                    let keep-test: adapt keep/ [
                        if not block? value [
                            panic "KEEP-TEST takes BLOCK! (acts as GROUP!)"
                        ]
                        value: quote as group! value
                    ]
                    keep: ~
                    (as group! body)
                ]]

                ; COLLECTED should just be a BLOCK! of groups now.
                ;
                flags: []

                sys.util/recover [
                    handler flags collected
                ]
                then (error -> [
                    log [space "error:" mold error newline]
                ])
                else [
                    log [space "success." newline]
                ]
            )
        ]
    ]
]

export do-recover: func [
    "Executes tests in the FILE and recovers from crash"

    return: [
        ~[file! text!]~  "log file, and textual summary of results"
    ]
    file [file!] "test file"
    flags [block!] "which flags to accept"
    code-checksum [<opt> blob!]
    log-file-prefix [file!]
    {
        interpreter last-vector value position next-position
        test-sources test-checksum
    }
][
    .allowed-flags: flags
    .successes: .test-failures: .crashes: .dialect-failures: .skipped: 0

    === CALCULATE TEST CHECKSUM ===

    ; !!! The test checksum was calculated on the file being given in.  But
    ; this isn't very useful anymore, since the test file can include other
    ; tests...it would need to be the cumulative checksum of all the tests
    ; in the run to know when you ran the same interpreter on the same tests...
    ;
    test-checksum: checksum 'sha1 (read file)

    === GENERATE NAME FOR LOG FILE FROM INTERPRETER AND TEST CHECKSUMS ===

    .log-file: clean-path join log-file-prefix [
        if code-checksum ["_"]
        if code-checksum [copy:part (skip mold code-checksum 2) 6]
        "_"
        copy:part (skip mold test-checksum 2) 6
        ".log"
    ]

    === TEMPOARILY DISABLE TEST CRASH RECOVERY ===

    ; !!! Crash recovery requires parsing the test log to see how far it got,
    ; and picking up after that.  It's an interesting feature but is not
    ; really used much right now--because if a crash happens it gets worked on
    ; and fixed.  Then the tests are run from the beginning.  But in the future
    ; it would be helpful in automated runs so a long-running test session
    ; does not become useless just because one crash happened.

    if exists? .log-file [delete .log-file]

    === COLLECT THE TESTS TO RUN (SOME MAY BE COMPLETED IF RECOVERING) ===

    if not exists? .log-file [
        print "new log"
        test-sources: collect-tests file
    ]
    else [
        parse3 read .log-file [
            (last-vector: null)
            opt some [
                opt some whitespace
                [
                    position: <here>

                    ; Test filenames appear in the log, %x.test.r
                    "%" (
                        next-position: null  ; !!! for SET-WORD! gather
                        [next-position value]: transcode:next position
                    )
                    seek (next-position)
                        |
                    ; dialect failure?
                    some whitespace
                    -["]- thru -["]-
                    (.dialect-failures: me + 1)
                        |
                    last-vector: across ["(" test-source-rule ")"]
                    opt some whitespace
                    [
                        <end> (
                            ; crash found
                            .crashes: me + 1
                            log [-[ "crashed"^/]-]
                        )
                            |
                        -["]- value: across to -["]- one
                        ; test result found
                        (
                            parse3 value [
                                "succeeded" <end>
                                (.successes: me + 1)
                                    |
                                "failed" opt ["," to <end>]  ; error msg
                                (.test-failures: me + 1)
                                    |
                                "crashed" <end>
                                (.crashes: me + 1)
                                    |
                                "skipped" <end>
                                (.skipped: me + 1)
                                    |
                                (panic "invalid test result")
                            ]
                        )
                    ]
                        |
                    "system.version:"
                    to <end>
                    (last-vector: null)
                ]
                    |
                panic [
                    "Log file parse problem, see"
                    mold:limit as text! position 240
                ]
            ]
            <end>
        ] except [
            panic "do-recover log file parsing problem"
        ]
        last-vector
        [_ test-sources]: find-last test-sources last-vector

        print [
            "recovering at:"
            (
                .successes
                + .test-failures
                + .crashes
                + .dialect-failures
                + .skipped
            )
        ]
    ]

    === RUN THE COLLECTED TESTS, RETURN LOG FILE AND SUMMARY TEXT ===

    if empty? test-sources [
        if summary [
            set summary "testing already complete"
        ]
        return .log-file
    ]

    let summary
    process-tests test-sources run-test-cluster/ then [
        summary: spaced [
            "system.version:" @system.version LF
            "code-checksum:" @code-checksum LF
            "test-checksum:" @test-checksum LF
            "Total:" (
                .successes
                + .test-failures
                + .crashes
                + .dialect-failures
                + .skipped
            ) LF
            "Succeeded:" .successes LF
            "Failing Tests:" .test-failures LF
            "Crashes:" .crashes LF
            "Failures in Test Dialect Usage:" .dialect-failures LF
            "Skipped:" .skipped LF
        ]

        log [summary]
    ]

    return pack [.log-file summary]
]
