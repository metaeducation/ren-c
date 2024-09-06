Rebol [
    Title: "Test parsing"
    File: %test-parsing.r
    Type: module
    Name: Test-Parsing
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

export whitespace: charset [#"^A" - #" " "^(7F)^(A0)"]
export digit: charset {0123456789}

success-rule: ~

position: ~
success: ~

; Note: This doesn't rely on LOAD to break a file full of tests down into
; individual tests.  LOAD is only used on a per-test basis.  The reason this
; extra hassle was done is so that if the scanner has a problem on one test,
; it only shows failure on that test...not the whole file.  Whether this is
; worth it or not is debatable, but rewriting string escaping in a parse rule
; here is certainly not worth it.  So when it sees a `{` or a `"` it defers
; to TRANSCODE to parse that string.

export test-source-rule: [
    opt some [
        let position: <here>

        ["{" | {"}] :(  ; handle string using TRANSCODE, see note
            trap [
                [position _]: transcode/next position
            ] then [
                'bypass  ; result for :() is rule to say stop the parse
            ] else [
                [seek position]  ; result for :() go to transcoded position
            ]
        )
            |
        ["{" | {"}] seek position
        break
            |
        "[" test-source-rule "]"  ; plain BLOCK! in code for a test
            |
        "(" test-source-rule ")"  ; plain GROUP! in code for a test
            |
        ";" [thru newline | to <end>]
            |
        ;
        ; If we see a closing bracket out of turn, that means we've "gone
        ; too far".  It's either a syntax error, or the closing bracket of
        ; a multi-test block.
        ;
        "]", seek position
        break
            |
        ")", seek position
        break
            |
        one
    ]
]

load-testfile: func [
    {Read the test source, preprocessing if necessary.}
    test-file [file!]
][
    let test-source: context [
        filepath: test-file
        contents: read test-file
    ]
    return test-source
]

export collect-tests: func [
    return: [block!]
    file "Name of file written in the test dialect to gather tests from"
        [file!]
    /into [block!]
][
    into: default [copy []]

    let current-dir: what-dir
    print ["file:" mold file]

    let code: load file
    let flags: copy []

    ; We're only loading the tests now.  But we keep the path of the test
    ; file in the stream of collected test blocks...so when we're actually
    ; running the tests later, the runner can use CHANGE-DIR to set the
    ; directory to where the test file was.
    ;
    append into clean-path file

    let pos
    let item
    let body
    append into spread collect [parse3 code [
        opt some [
            pos: <here>

            ; A GROUP! top level in the test file indicates a standalone test.
            ; Put it in a BLOCK! to denote that it should run in an isolated
            ; context, but all by itself.
            ;
            ; Also, accommodate new feature, marking an expected error ID, e.g.
            ;
            ;    ~bad-pick~ !! (pick #{00} 'x)
            [
                expected: quasiform! ['!! | (fail "!! must follow ~error-id~")]
                |
                (expected: null)
            ]
            group: group! (
                keep flags, flags: copy []

                ; Treat a top level group (...) as if you wrote [(...)].
                ; Put it in a block, along with its optional expected error ID.
                ;
                keep/line reduce [(maybe expected) (if expected '!!) group]
            )
            |
            ; A BLOCK! groups together several tests that rely on common
            ; definitions.  They are isolated together.  The block is not
            ; checked for syntax--it is used to create a module, then it
            ; runs all groups that start newlines as tests...interleaved
            ; with whatever other code there is.
            ;
            item: block! (
                keep flags, flags: copy []
                keep/line item
            )
            |
            ; ISSUE! and URL! have historically just been ignored, they are a
            ; kind of comment that you don't have to put a semicolon on.  It
            ; may be they have a better dialect use, perhaps even automated
            ; tests could keep track of how often a particular issue or URL
            ; had a bad test and mark it as "active"?
            ;
            [url! | issue!] (~noop~)
            |
            ; Tags represent flags which can control the behavior of tests.
            ; Each test grouping resets the flags here, but that suggests
            ; the flags should probably go in blocks (?)  Unclear what
            ; the feature will shape up to be, but there were stray tags,
            ; so just collect them... nothing looks at them right now.
            ;
            item: tag! (
                append flags item
            )
            |
            ; The test dialect should probably let you reference a file from
            ; another file, to sub-factor tests.  Right now the top level
            ; %core-tests.r is the only one that accepts subfiles.
            ;
            item: file! (
                let referenced-file: item

                change-dir maybe split-path file
                collect-tests/into referenced-file into
                change-dir current-dir
            )
            |
            ; New feature: test generation and collection
            ;
            '@collect-tests, body: block! (
                keep @collect-tests
                keep body
            )
        ]  ; ends TRY SOME
    ] except [  ; ends PARSE
        append into spread reduce [
            'dialect
            spaced [
                newline
                {"failed, line/col:} (line of pos) {"}  ; no column, parsed
                newline
            ]
        ]
    ]]

    return into
]

export collect-logs: func [
    return: [~]
    collected-logs [block!]
        {collect the logged results here (modified)}
    log-file [file!]
][
    let log-contents: read log-file except [
        fail ["Unable to read " mold log-file]
    ]

    let guard
    let value
    let last-vector
    let position
    let next-position
    parse3 log-contents [
        (guard: false)  ; trigger failure by default (may be set to true)
        opt some [
            opt some whitespace
            [
                position: "%"
                (next-position: transcode/next (the value:) position)
                seek next-position
                    |
                ; dialect failure?
                some whitespace
                {"} thru {"}
                    |
                last-vector: across ["(" test-source-rule ")"]
                opt some whitespace
                [
                    <end> (
                        ; crash found
                        fail "log incomplete!"
                    )
                        |
                    {"} copy value to {"} one
                    ; test result found
                    (
                        parse3 value [
                            "succeeded" (value: 'succeeded)
                                |
                            "failed" (value: 'failed)
                                |
                            "crashed" (value: 'crashed)
                                |
                            "skipped" (value: 'skipped)
                                |
                            (fail "invalid test result")
                        ]
                        append collected-logs spread reduce [
                            last-vector
                            value
                        ]
                    )
                ]
                    |
                "system.version:" to <end> (guard: true)
                    |
                (fail "collect-logs - log file parsing problem")
            ]
            position: <here>, guard, break ; Break when error detected.
                |
            seek position
        ]
    ]
]
