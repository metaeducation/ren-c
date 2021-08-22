Rebol [
    Title: "Test parsing"
    File: %test-parsing.r
    Type: module
    Name: Test-Parsing
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

import %line-numberq.r
import %../tools/parsing-tools.reb
import %../tools/text-lines.reb

whitespace: charset [#"^A" - #" " "^(7F)^(A0)"]
digit: charset {0123456789}

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

test-source-rule: [
    while [
        let position: here

        ["{" | {"}] :(  ; handle string using TRANSCODE, see note
            trap [
                [# position]: transcode position
            ] then [
                [false]  ; result for :() is rule to say stop the parse
            ] else [
                [seek position]  ; result for :() go to transcoded position
            ]
        )
            |
        ["{" | {"}] seek :position, break
            |
        "[" test-source-rule "]"  ; plain BLOCK! in code for a test
            |
        "(" test-source-rule ")"  ; plain GROUP! in code for a test
            |
        ";" [thru newline | to end]
            |
        ;
        ; If we see a closing bracket out of turn, that means we've "gone
        ; too far".  It's either a syntax error, or the closing bracket of
        ; a multi-test block.
        ;
        "]", seek :position, break
            |
        ")", seek :position, break
            |
        skip
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

collect-tests: function [
    return: <none>
    collected-tests [block!]
        {collect the tests here (modified)}
    test-file [file!]
][
    current-dir: what-dir
    print ["file:" mold test-file]

    trap [
        if file? test-file [
            test-file: clean-path test-file
            change-dir first split-path test-file
        ]
        test-sources: get in load-testfile test-file 'contents
        ensure binary! test-sources  ; this is how they are passed ATM
    ] then err -> [
        ; probe err
        append collected-tests reduce [
            test-file 'dialect {^/"failed, cannot read the file"^/}
        ]
        change-dir current-dir
        return
    ] else [
        change-dir current-dir
        append collected-tests test-file
    ]

    types: context [
        wsp: cmt: val: tst: grpb: grpe: flg: fil: isu: str: url: end: _
    ]

    flags: copy []

    wsp: [
        [
            some whitespace (type: in types 'wsp)
            |
            ";" [thru newline | to end] (type: in types 'cmt)
        ]
    ]

    any-wsp: [while [wsp emit-token]]

    single-value: parsing-at x [
        let next-position
        trap [
            value: _  ; !!! for collecting with SET-WORD!, evolving
            next-position: _  ; !!! ...same
            [value next-position]: transcode x
        ] else [
            type: in types 'val
            next-position
        ]
    ]

    single-test: [
        let vector: across ["(" test-source-rule ")"] (
            type: in types 'tst
            append/only collected-tests flags
            append collected-tests vector
        )
    ]

    grouped-tests: [
        "[" (type: in types 'grpb) emit-token
        opt [
            any-wsp single-value
            :(text? value) (type: in types 'str)
            emit-token
        ]
        while [
            any-wsp single-value
            [
                :(tag? value) (
                    type: in types 'flag
                    append flags value
                )
                    |
                :(issue? value) (type: in types 'isu)
                    |
                :(url? value) (type: in types 'url)
            ]
            emit-token
        ]
        while [any-wsp single-test emit-token]
        any-wsp "]" (type: in types 'grpe) emit-token
    ]

    token: [
        position: here

        (type: value: _)

        wsp emit-token
            |
        single-test (flags: copy []) emit-token
            |
        grouped-tests (flags: copy [])
            |
        end (type: in types 'end) emit-token break
            |
        single-value
        [
            :(tag? get 'value) (
                type: in types 'flg
                append flags value
            )
            |
            :(file? get 'value) (
                type: in types 'fil
                collect-tests collected-tests value
                print ["file:" mold test-file]
                append collected-tests test-file
            )
        ]
    ]

    emit-token: [
        token-end: here, (
            comment [
                prin "emit: " probe compose [
                    (type) (to text! copy/part position token-end)
                ]
            ]
        )
        position: here, (type: value: _)
    ]

    rule: [while token]

    parse test-sources rule else [
        append collected-tests reduce [
            'dialect
            spaced [
                newline
                {"failed, line/col:} (text-location-of position) {"}
                newline
            ]
        ]
    ]
]

collect-logs: function [
    collected-logs [block!]
        {collect the logged results here (modified)}
    log-file [file!]
][
    trap [log-contents: read log-file] then [
        fail ["Unable to read " mold log-file]
    ]

    parse log-contents [
        (guard: false)  ; trigger failure by default (may be set to true)
        while [
            while whitespace
            [
                position: "%"
                (next-position: transcode/next (the value:) position)
                seek next-position
                    |
                ; dialect failure?
                some whitespace
                {"} thru {"}
                    |
                copy last-vector ["(" test-source-rule ")"]
                while whitespace
                [
                    end (
                        ; crash found
                        fail "log incomplete!"
                    )
                        |
                    {"} copy value to {"} skip
                    ; test result found
                    (
                        parse value [
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
                        append collected-logs reduce [
                            last-vector
                            value
                        ]
                    )
                ]
                    |
                "system/version:" to end (guard: true)
                    |
                (fail "collect-logs - log file parsing problem")
            ]
            position: here, guard, break ; Break when error detected.
                |
            seek position
        ]
        end
    ]
]

export [collect-tests collect-logs]
