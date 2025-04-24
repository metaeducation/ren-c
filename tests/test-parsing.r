Rebol [
    Title: "Test parsing"
    File: %test-parsing.r
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

do %line-numberq.r

null-to-blank: func [x [any-value!]] [either null? x [_] [:x]]

parse2: :parse/redbol

parsing-at: func [  ; redefined here for <here> usage in regular PARSE
    {Defines a rule which evaluates a block for the next input position, fails otherwise.}
    'word [word!] {Word set to input position (will be local).}
    block [block!]
        {Block to evaluate. Return next input position, or blank/false.}
    /end {Drop the default tail check (allows evaluation at the tail).}
] [
    use [result position][
        block: compose/only [null-to-blank (as group! block)]
        if not end [
            block: compose/deep [either not tail? (word) [(block)] [_]]
        ]
        block: compose/deep [result: either position: (block) [[seek position]] [[<end> one]]]
        use compose [(word)] compose/deep [
            [(as set-word! :word) <here>
            (as group! block) result]
        ]
    ]
]

do %../tools/text-lines.reb

whitespace: charset [#"^A" - #" " "^(7F)^(A0)"]
digit: charset {0123456789}


read-binary: :read

make object! [

    position: null
    success: null
    dummy: null

    ;; TEST-SOURCE-RULE matches the internal text of a test
    ;; even if that text is invalid rebol syntax.

    set 'test-source-rule [
        opt some [
            position: <here>

            ; can't transcode "^(00)" as string but want #"^(00)" to work
            ; look ahead for non-escaped quote, accounting for special case
            ; when a double caret might precede a quote.
            ;
            {#"} some [{^^"} | {^^^^} | not ahead {"} one] {"}
                |
            ["{" | {"}] (
                ; handle string using TRANSCODE
                success-rule: trap [
                    if error? position: transcode/next3 position 'dummy [
                        fail position
                    ]
                ] then [
                    [<end> one]
                ] else [
                    [seek position]
                ]
            ) success-rule
                |
            ["{" | {"}] seek position break
                |
            "[" test-source-rule "]" ;-- plain BLOCK! in code for a test
                |
            "(" test-source-rule ")" ;-- plain GROUP! in code for a test
                |
            ";" [thru newline | to <end>]
                |
            ;
            ; If we see a closing bracket out of turn, that means we've "gone
            ; too far".  It's either a syntax error, or the closing bracket of
            ; a multi-test block.
            ;
            "]" seek position break
                |
            ")" seek position break
                |
            one
        ]
    ]

    set 'load-testfile function [
        {Read the test source, preprocessing if necessary.}
        test-file [file!]
    ][
        test-source: context [
            filepath: test-file
            contents: read test-file
        ]
        test-source
    ]

    set 'collect-tests function [
        return: [~]
        collected-tests [block!]
            {collect the tests here (modified)}
        test-file [file!]
    ][
        current-dir: what-dir
        print ["file:" mold test-file]

        sys/util/rescue [
            if file? test-file [
                test-file: clean-path test-file
                change-dir split-path test-file
            ]
            test-sources: get in load-testfile test-file 'contents
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
            wsp: cmt: val: tst: grpb: grpe: flg: fil: isu: str: end: null
        ]

        flags: copy []

        wsp: [
            [
                some whitespace (type: in types 'wsp)
                |
                ";" [thru newline | to <end>] (type: in types 'cmt)
            ]
        ]

        any-wsp: [opt some [wsp emit-token]]

        single-value: parsing-at x [
            sys/util/rescue [
                next-position: transcode/next3 x the value:
            ] else [
                type: in types 'val
                next-position
            ]
        ]

        single-test: [
            vector: across ["(" test-source-rule ")"] (
                type: in types 'tst
                append/only collected-tests flags
                append collected-tests vector
            )
        ]

        grouped-tests: [
            "[" (type: in types 'grpb) emit-token
            opt some [
                any-wsp single-value
                [
                    when (tag? value) (
                        type: in types 'flag
                        append flags value
                    )
                        |
                    when (issue? value) (type: in types 'isu)
                ]
                emit-token
            ]
            opt [
                any-wsp single-value
                when (text? value) (type: in types 'str)
                emit-token
            ]
            opt some [any-wsp single-test emit-token]
            any-wsp "]" (type: in types 'grpe) emit-token
        ]

        token: [
            position: <here>

            (type: value: null)

            wsp emit-token
                |
            single-test (flags: copy []) emit-token
                |
            grouped-tests (flags: copy [])
                |
            <end> (type: in types 'end) emit-token break
                |
            single-value
            [
                when (tag? get 'value) (
                    type: in types 'flg
                    append flags value
                )
                |
                when (file? get 'value) (
                    type: in types 'fil
                    collect-tests collected-tests value
                    print ["file:" mold test-file]
                    append collected-tests test-file
                )
            ]
        ]

        emit-token: [
            token-end: <here> (
                comment [
                    prin "emit: " probe compose [
                        (type) (to text! copy/part position token-end)
                    ]
                ]
            )
            position: <here> (type: value: null)
        ]

        rule: [opt some token <end>]

        parse/match test-sources rule else [
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

    set 'collect-logs function [
        collected-logs [block!]
            {collect the logged results here (modified)}
        log-file [file!]
    ][
        sys/util/rescue [log-contents: read log-file] then [
            fail ["Unable to read " mold log-file]
        ]

        parse log-contents [
            (guard: [<end> one])
            opt some [
                opt some whitespace
                [
                    position: "%"
                    (next-position: transcode/next3 position the value)
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
                        {"} value: across to {"} one
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
                    "system/version:" to <end> (guard: null)
                        |
                    (fail "collect-logs - log file parsing problem")
                ] position: <here> guard break ; Break when error detected.
                    |
                seek position
            ]
        ]
    ]
]
