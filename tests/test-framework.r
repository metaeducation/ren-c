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

log: func [report [block!]] [
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

    code "Code GROUP! from test file, assumed bound into isolated module"
        [group!]
][
    log [mold code]

    let [error result]: trap as block! code
    case [
        error [
            spaced ["error" any [to text! error.id, "w/no ID"]]
        ]
        bad-word? ^result [
            "test returned" (mold ^result) "(isotope)"
        ]
        null? :result [
            "test returned null"
        ]
        not logic? :result [
            spaced ["was" (an type of :result) ", not logic!"]
        ]
        not :result [
            "test returned #[false]"
        ]
    ] then message -> [
        test-failures: me + 1
        log reduce [space {"failed, } message {"} newline]
    ] else [
        successes: me + 1
        log reduce [space {"succeeded"} newline]
    ]
]

run-test-cluster: func [
    return: <none>
    flags [block!]
    cluster "Block of GROUP!s to be run together in a common isolated context"
        [block!]
    <with> test-failures successes skipped
][
    if not empty? exclude flags allowed-flags [
        skipped: me + 1
        log [space {"skipped"} newline]
        return
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
    let isolate: module _ [
        print: func [x] [
            fail "Don't use PRINT in tests"
        ]
    ]

    ; Since we didn't use our cluster block as the body of the module, we have
    ; to bind into it.  The form of binding we use here is called "attachment"
    ; and it is done with the INTERN function.
    ;
    ; INTERN without a * is specialized to be single arity, and intern code
    ; into this module (e.g. %test-framework.r).  The lower-level INTERN* is
    ; arity-2, and we can tell it to bind this whole block-of-groups-of-code
    ; into the isolation context we just created.
    ;
    intern* isolate cluster

    ; Code that begins a line and starts a GROUP! will be checked as a test.
    ; Everything else will be executed and its results discarded.  This
    ; permits sharing, cleanup, and opens many other possibilities.
    ;
    ;    [
    ;        port: (open %something.txt)  ; group not line start--not a test
    ;        foo: func [x] [x + 1]  ; not a test
    ;
    ;        (2 = foo 1)  ; a test--result is checked and logged
    ;        (3 = foo 2)  ; a test--result is checked and logged
    ;        (port? port)  ; a test--result is checked and logged
    ;
    ;        close port  ; not a test
    ;    ]
    ;
    ; !!! Right now all the GROUP! tests are run in the same module as the
    ; cluster, but ideally they would run in a module per-group that just
    ; *inherited* from the cluster.  Hopefully modules will be able to do
    ; things like that in the "near future"(tm).
    ;
    for-next pos cluster [
        ;
        ; !!! Skip any ISSUE! or URL!, as this style has been used:
        ;
        ;     [#123 http://example.com (
        ;        2 = 1 + 1
        ;     )]
        ;
        ; This test style does not put the GROUP! on its own line.  So if we
        ; encounter a GROUP! in such a state we consider it a test.
        ;
        loop [not tail? pos] [
            match [url! issue!] pos.1 else [break]
            pos: next pos
        ]

        ; If we're at a GROUP! then assume it's a test, otherwise look ahead
        ; for the next GROUP! that starts a line.
        ;
        group-pos: if group? pos.1 [pos] else [catch [
            for-next p pos [
                if (group? p.1) and (new-line? p) [
                    throw p
                ]
            ]
        ]]

        ; Run any code that's between pos and the next group.  Note that
        ; GROUP-POS as NULL will opt out of the refinement and copy to end.
        ;
        ; !!! Temporary disabled:
        ; https://forum.rebol.info/t/1680
        ;
        if group-pos <> pos [
            let code: copy/part pos group-pos
            trap [
                log [mold code newline]
                fail "out-of-GROUP disabled: https://forum.rebol.info/t/1680/"
                do code  ; result of DO is discarded
            ] then error -> [
                ;
                ; If common code for a cluster fails, the error is logged and
                ; no more tests in the cluster are run.
                ;
                log ["Error in cluster code:" mold error newline]
                return
            ]
        ]

        ; If we didn't find another group, we just ran all the code.  Done!
        ;
        if not group-pos [
            break
        ]

        pos: group-pos

        ; There's a newline-starting GROUP! at group-pos.  Run it as a test,
        ; and advance the position.
        ;
        ; We tolerate tests that aren't on new lines, but abut a previous test.
        ;
        ;     [(
        ;         304 = 300 + 4
        ;     )(  ; <-- this group has a newline internally, not externally
        ;         1020 = 1000 + 20
        ;     )]
        ;
        ; It's kind of ugly to do that, but many tests did it before the new
        ; test facilities were available.  We'll try and improve things.
        ;
        loop [group? pos.1] [
            run-single-test pos.1
            pos: next pos
        ]
    ]
]


; The tests are collected in a pre-phase with COLLECT-TESTS.  It produces a
; long list of BLOCK!s that are test groups.
;
process-tests: function [
    return: <none>
    test-sources [block!]
    handler [action!]
][
    parse test-sources [
        while [
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
                change-dir first split-path test-file
            )
                |
            'dialect set value: text! (  ; bad parse of test file itself
                log [value]
                set 'dialect-failures (dialect-failures + 1)
            )
                |
            'collect-tests set body: block! (
                log ["@collect-tests" space mold body]
                trap [
                    let [# collected]: module _ compose/deep [collect [
                        let keep-test: adapt :keep [
                            if not block? :value [
                                fail "KEEP-TEST takes BLOCK! (acts as GROUP!)"
                            ]
                            value: quote as group! value
                        ]
                        keep: ~unset~
                        (as group! body)
                    ]]

                    ; COLLECTED should just be a BLOCK! of groups now.
                    ;
                    let flags: []
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
        end
    ]
]

export do-recover: func [
    {Executes tests in the FILE and recovers from crash}

    return: "The log file that was generated"
        [file!]
    summary: "Textual summary of the test results"
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

    log-file: clean-path join log-file-prefix unspaced :[
        if code-checksum :["_", copy/part (skip mold code-checksum 2) 6]
        "_", copy/part (skip mold test-checksum 2) 6, ".log"
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
        parse read log-file [
            (last-vector: _)
            while [
                while whitespace
                [
                    position: here

                    ; Test filenames appear in the log, %x.test.reb
                    "%" (
                        next-position: _  ; !!! for SET-WORD! gather
                        [value next-position]: transcode position
                    )
                    seek :next-position
                        |
                    ; dialect failure?
                    some whitespace
                    {"} thru {"}
                    (dialect-failures: dialect-failures + 1)
                        |
                    copy last-vector ["(" test-source-rule ")"]
                    while whitespace
                    [
                        end (
                            ; crash found
                            crashes: crashes + 1
                            log [{ "crashed"^/}]
                        )
                            |
                        {"} copy value to {"} skip
                        ; test result found
                        (
                            parse value [
                                "succeeded" end
                                (successes: me + 1)
                                    |
                                "failed" opt ["," to end]  ; error msg
                                (test-failures: me + 1)
                                    |
                                "crashed" end
                                (crashes: me + 1)
                                    |
                                "skipped" end
                                (skipped: me + 1)
                                    |
                                (fail "invalid test result")
                            ]
                        )
                    ]
                        |
                    "system/version:"
                    to end
                    (last-vector: _)
                ]
                    |
                (fail [
                    "Log file parse problem, see"
                    mold/limit as text! position 240
                ])
            ]
            end
        ] else [
            fail "do-recover log file parsing problem"
        ]
        last-vector
        test-sources: find-last/tail test-sources last-vector

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
        let temp: spaced [
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
            "Failing Tests:" test-failures LF
            "Crashes:" crashes LF
            "Failures in Test Dialect Usage:" dialect-failures LF
            "Skipped:" skipped LF
        ]

        log [temp]
        if summary [set summary temp]
    ]

    return log-file
]
