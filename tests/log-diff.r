Rebol [
    title: "Log diff"
    file: %log-diff.r
    copyright: [2012 "Saphirion AG"]
    license: --[
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    ]--
    author: "Ladislav Mecir"
    purpose: "Test framework"
]

import %test-parsing.r

make-diff: proc [
    old-log [file!]
    new-log [file!]
    diff-file [file!]
][
    if exists? diff-file [delete diff-file]

    collect-logs old-log-contents: copy [] old-log
    collect-logs new-log-contents: copy [] new-log

    sort:case:skip old-log-contents 2
    sort:case:skip new-log-contents 2

    ; counter initialization
    let new-successes: 0
    let new-failures: 0
    let new-crashes: 0
    let progressions: 0
    let regressions: 0
    let removed: 0
    let unchanged: 0

    ; cycle initialization
    let old-test: old-log-contents.1
    let old-result: old-log-contents.2
    old-log-contents: skip old-log-contents 2

    let new-test: new-log-contents.1
    let new-result: new-log-contents.2
    new-log-contents: skip new-log-contents 2

    while [any [old-test new-test]] [
        case [
            all [
                new-test
                new-result <> 'skipped
                any [
                    space? old-test
                    all [
                        old-test != new-test
                        old-test = second sort:case reduce [new-test old-test]
                    ]
                    all [
                        old-test = new-test
                        old-result = 'skipped
                    ]
                ]
            ] [
                ; fresh test
                write:append diff-file spaced [
                    new-test
                    switch new-result [
                        'succeeded [
                            new-successes: new-successes + 1
                            "succeeded"
                        ]
                        'failed [
                            new-failures: new-failures + 1
                            "failed"
                        ]
                        'crashed [
                            new-crashes: new-crashes + 1
                            "crashed"
                        ]
                    ]
                    newline
                ]
            ]
            all [
                old-test
                old-result <> 'skipped
                any [
                    space? new-test
                    all [
                        new-test != old-test
                        new-test = second sort:case reduce [old-test new-test]
                    ]
                    all [
                        new-test = old-test
                        new-result = 'skipped
                    ]
                ]
            ] [
                ; removed test
                removed: removed + 1
                write:append diff-file spaced [old-test "removed" newline]
            ]
            any [
                old-result = new-result
                old-test != new-test
            ] [unchanged: unchanged + 1]
            ; having one test with different results
            (
                write:append diff-file new-test
                any [
                    old-result = 'succeeded
                    all [
                        old-result = 'failed
                        new-result = 'crashed
                    ]
                ]
            ) [
                ; regression
                regressions: regressions + 1
                write:append diff-file spaced [
                    space "regression," new-result newline
                ]
            ]
        ]
        else [
            ; progression
            progressions: progressions + 1
            write:append diff-file spaced [
                space "progression," new-result newline
            ]
        ]

        next-old-log: all [
            old-test
            any [
                space? new-test
                old-test = first sort:case reduce [old-test new-test]
            ]
        ]
        next-new-log: all [
            new-test
            any [
                space? old-test
                new-test = first sort:case reduce [new-test old-test]
            ]
        ]
        if next-old-log [
            if old-test = pick old-log-contents 1 [
                print old-test
                fail "duplicate test in old-log"
            ]
            set [old-test old-result] old-log-contents
            old-log-contents: skip old-log-contents 2
        ]
        if next-new-log [
            if new-test = pick new-log-contents 1 [
                print new-test
                fail "duplicate test in new-log"
            ]
            set [new-test new-result] new-log-contents
            new-log-contents: skip new-log-contents 2
        ]
    ]

    print "Done."

    summary: spaced [
        "new-successes:" new-successes
            |
        "new-failures:" new-failures
            |
        "new-crashes:" new-crashes
            |
        "progressions:" progressions
            |
        "regressions:" regressions
            |
        "removed:" removed
            |
        "unchanged:" unchanged
            |
        "total:"
            new-successes + new-failures + new-crashes + progressions
            + regressions + removed + unchanged
    ]
    print summary

    write:append diff-file unspaced [
        newline
        "Summary:" newline
        summary newline
    ]
]

make-diff to-file first load system.script.args to-file second load system.script.args %diff.r
