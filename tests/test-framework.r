Rebol [
    Title: "Test Framework"
    File: %test-framework.r
    Type: module
    Name: Test-Framework
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
]

import %test-parsing.r

log-file: ~

log: func [
    return: [~]
    report [block!]
][
    write/append log-file unspaced report

    ; By default we echo the log to the screen also.  This should be an option.
    ;
    write-stdout unspaced report
]

; counters
;
skipped: ~
test-failures: ~
crashes: ~
dialect-failures: ~
successes: ~

allowed-flags: ~

test-block: ~

error: ~

run-single-test: func [
    {Run code and write the success or failure to the log file}

    return: [~]
    code "Code GROUP! from test file, assumed bound into isolated module"
        [group!]
    expected-id [~null~ word!]
][
    assert [expected-id <> '!!!]  ; dialecting mistake, vs. ???, easy to make

    log [mold code]

    ; Need to do ^result, otherwise nihil (empty pack) would case a failure
    ; in the test code itself trying to unpack to a regular result.  We want
    ; to report it as a failure, not fail ourseves...
    ;
    let [error ^result]: sys.util.rescue+ as block! code

    all [
        error
        expected-id
        (all [not error.id, expected-id = '???]) or (error.id = expected-id)
    ] then [
        successes: me + 1
        log reduce [_ {"correct failure:"} _ quote quasi expected-id newline]
        return ~
    ]

    case [
        error [
            spaced ["error" any [
                to text! maybe error.id
                mold error.message   ; errors with no ID may have BLOCK!
                "(unknown)"
            ]]
        ]

        expected-id [
            spaced ["did not error, but expected:" mold quasi expected-id]
        ]

        nihil' = result [
            "test returned empty pack ~[]~ antiform"  ; UNMETA fails
        ]
        (elide if pack? unmeta result [result: first unquasi result])

        result = '~true~ [
            successes: me + 1
            log reduce [_ {"succeeded"} newline]
            return ~
        ]

        result = '~false~ [
            "test returned false"
        ]

        quasi? result [
            "test returned antiform:" (mold/limit result 40)
        ]
        (elide result: unmeta result)

        null? result [
            "test returned null"
        ]
        void? result [
            "test returned void"
        ]
        true [
            spaced ["was" (mold kind of :result) ", not ~true~ or ~false~"]
        ]
    ] then message -> [
        test-failures: me + 1
        log reduce [space {"failed, } message {"} newline]
    ]
]

run-test-cluster: func [
    return: [~]
    flags [block!]
    cluster "Block of GROUP!s to be run together in a common isolated context"
        [block!]
    <with> test-failures successes skipped
][
    if not empty? exclude flags allowed-flags [
        skipped: me + 1
        log [space {"skipped"} newline]
        return ~
    ]

    ; Here we use MODULE instead of MAKE MODULE! so that we get IMPORT and
    ; INTERN available.  (MAKE MODULE! is lower level, and makes a completely
    ; empty context).  We pass a BLANK! instead of a header to indicate that
    ; it is a "do-style" context (this result also comes back from DO* as
    ; a secondary result, which could be useful as this feature expands.)
    ;
    ; We don't want the tests (represented with GROUP!) in the body block of
    ; the module, because we don't want to run them all in a batch.  But we
    ; could put service functions that tests could use there, e.g. such as
    ; specialized versions of assert.
    ;
    ; Modules created with module "inherit" from LIB by default.
    ;
    let isolate: module null [
        print: lambda [x] [
            fail 'x "Don't use PRINT in tests"
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
    let group
    parse cluster [while not <end> [
        ;
        ; !!! Skip any ISSUE! or URL!, as this style has been used:
        ;
        ;     [#123 http://example.com (
        ;        2 = 1 + 1
        ;     )]
        ;
        try some [url! | issue!]

        (expected-id: null)  ; default expects a true result, not error w/id

        try [
            expected-id: quasiform! [(expected-id: unquasi expected-id)
                '!! ahead group!
                | (fail "~error-id~ must be followed by !! and a GROUP!")
            ]
        ]
        [
            group: group!
            | (fail "GROUP! expected in tests")
        ]
        (run-single-test inside isolate group expected-id)
    ]]
]


; The tests are collected in a pre-phase with COLLECT-TESTS.  It produces a
; long list of BLOCK!s that are test groups.
;
process-tests: func [
    return: [~]
    test-sources [block!]
    handler [action?]
][
    let flags
    let value
    let test-file
    let body
    let collected
    parse3 test-sources [
        try some [
            set flags: block! set value: block! (
                handler flags value  ; flags ignored atm
            )
                |
            set test-file: file! (
                log ["^/" mold test-file "^/^/"]
                ;
                ; We'd like tests to be able to live anywhere on disk
                ; (e.g. extensions can have a %tests/ subdirectory).  If
                ; those tests have supplementary scripts or data files,
                ; the test should be able to refer to them via paths
                ; relative the directory where the test is running.  So
                ; we CHANGE-DIR to the test file's path.
                ;
                change-dir [_ @]: split-path test-file
            )
                |
            'dialect set value: text! (  ; bad parse of test file itself
                log [value]
                set $dialect-failures (dialect-failures + 1)
            )
                |
            'collect-tests set body: block! (
                log ["@collect-tests" space mold body]

                let [_ collected]: module null compose/deep [collect [
                    let keep-test: adapt :keep [
                        if not block? :value [
                            fail "KEEP-TEST takes BLOCK! (acts as GROUP!)"
                        ]
                        value: quote as group! value
                    ]
                    keep: ~
                    (as group! body)
                ]]

                ; COLLECTED should just be a BLOCK! of groups now.
                ;
                flags: []

                sys.util.rescue+ [
                    handler flags collected
                ]
                then error -> [
                    log [space "error:" mold error newline]
                ]
                else [
                    log [space "success." newline]
                ]
            )
        ]
    ]
]

export do-recover: func [
    {Executes tests in the FILE and recovers from crash}

    return: "The log file that was generated"
        [file!]
    @summary "Textual summary of the test results"
        [text!]
    file [file!] {test file}
    flags [block!] {which flags to accept}
    code-checksum [binary! blank!]
    log-file-prefix [file!]
    <local>
        interpreter last-vector value position next-position
        test-sources test-checksum
][
    allowed-flags: flags
    successes: test-failures: crashes: dialect-failures: skipped: 0

    === CALCULATE TEST CHECKSUM ===

    ; !!! The test checksum was calculated on the file being given in.  But
    ; this isn't very useful anymore, since the test file can include other
    ; tests...it would need to be the cumulative checksum of all the tests
    ; in the run to know when you ran the same interpreter on the same tests...
    ;
    test-checksum: checksum 'sha1 (read file)

    === GENERATE NAME FOR LOG FILE FROM INTERPRETER AND TEST CHECKSUMS ===

    log-file: clean-path join log-file-prefix unspaced [
        if code-checksum ["_"]
        if code-checksum [copy/part (skip mold code-checksum 2) 6]
        "_"
        copy/part (skip mold test-checksum 2) 6
        ".log"
    ]

    === TEMPOARILY DISABLE TEST CRASH RECOVERY ===

    ; !!! Crash recovery requires parsing the test log to see how far it got,
    ; and picking up after that.  It's an interesting feature but is not
    ; really used much right now--because if a crash happens it gets worked on
    ; and fixed.  Then the tests are run from the beginning.  But in the future
    ; it would be helpful in automated runs so a long-running test session
    ; does not become useless just because one crash happened.

    if exists? log-file [delete log-file]

    === COLLECT THE TESTS TO RUN (SOME MAY BE COMPLETED IF RECOVERING) ===

    if not exists? log-file [
        print "new log"
        test-sources: collect-tests file
    ]
    else [
        parse3 read log-file [
            (last-vector: null)
            try some [
                try some whitespace
                [
                    position: <here>

                    ; Test filenames appear in the log, %x.test.reb
                    "%" (
                        next-position: null  ; !!! for SET-WORD! gather
                        [value next-position]: transcode/one position
                    )
                    seek (next-position)
                        |
                    ; dialect failure?
                    some whitespace
                    {"} thru {"}
                    (dialect-failures: dialect-failures + 1)
                        |
                    copy last-vector ["(" test-source-rule ")"]
                    try some whitespace
                    [
                        <end> (
                            ; crash found
                            crashes: crashes + 1
                            log [{ "crashed"^/}]
                        )
                            |
                        {"} copy value to {"} skip
                        ; test result found
                        (
                            parse3 value [
                                "succeeded" <end>
                                (successes: me + 1)
                                    |
                                "failed" try ["," to <end>]  ; error msg
                                (test-failures: me + 1)
                                    |
                                "crashed" <end>
                                (crashes: me + 1)
                                    |
                                "skipped" <end>
                                (skipped: me + 1)
                                    |
                                (fail "invalid test result")
                            ]
                        )
                    ]
                        |
                    "system.version:"
                    to <end>
                    (last-vector: null)
                ]
                    |
                (fail [
                    "Log file parse problem, see"
                    mold/limit as text! position 240
                ])
            ]
            <end>
        ] except [
            fail "do-recover log file parsing problem"
        ]
        last-vector
        [_ test-sources]: find-last test-sources last-vector

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
    ]

    === RUN THE COLLECTED TESTS, RETURN LOG FILE AND SUMMARY TEXT ===

    if empty? test-sources [
        if summary [
            set summary "testing already complete"
        ]
        return log-file
    ]

    process-tests test-sources :run-test-cluster then [
        summary: spaced [
            "system.version:" system.version LF
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
            "Failing Tests:" test-failures LF
            "Crashes:" crashes LF
            "Failures in Test Dialect Usage:" dialect-failures LF
            "Skipped:" skipped LF
        ]

        log [summary]
    ]

    return log-file
]
