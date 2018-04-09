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
do %../make/tools/parsing-tools.reb
do %../make/tools/text-lines.reb

whitespace: charset [#"^A" - #" " "^(7F)^(A0)"]
digit: charset {0123456789}


read-binary: :read

make object! [

    position: _
    success: _

    ;; TEST-SOURCE-RULE matches the internal text of a test
    ;; even if that text is invalid rebol syntax.

    set 'test-source-rule [
        any [
            position: ["{" | {"}] (
                ; handle string using TRANSCODE
                success: either error? try [
                    position: second transcode/next position
                ] [
                    [end skip]
                ] [
                    [:position]
                ]
            ) success
                |
            ["{" | {"}] :position break
                |
            "[" test-source-rule "]"
                |
            "(" test-source-rule ")"
                |
            ";" [thru newline | to end]
                |
            "]" :position break
                |
            ")" :position break
                |
            skip
        ]
    ]

    ; -----------------------------------------------------------------------
    ; Temporary code section
    ; The following scanner, tokeniser and renderer are used for conversion
    ; to the new test format (grouped tests).
    ; It's presence allows developers to upgrade their own test code bases,
    ; once that has been done this code can be removed.

    make-original-tests-scanner: function [
        {Create a scanner for original test format.}
    ][
        context [
            type: ; Token type.
            t1: t2: _ ; Token begin and end.
            d1: d2: _ ; Token data begin and end.
            value: _ ; Token value.
            eol: _

            position: next-position: _

            do-scanned: _ ; Token scan event function.
            types: context [eol: wsp: cmt: tst: flg: fil: end: _]

            single-value: parsing-at x [
                if not error? try [
                    set [value: next-position:] transcode/next x
                ][
                    next-position
                ]
            ]

            rule: [
                ; EOL convention.
                position:
                any not-eolc
                [crlf (set 'eol crlf) | newline (set eol 'newline)]
                :position

                any [
                    (type: value: t2: d2: _) t1: d1:
                    [
                        eol (type: in types 'eol)
                            |
                        some whitespace (type: in types 'wsp)
                            |
                        ";" d1: [to eol d2: thru newline | to end d2:] (type: in types 'cmt)
                            |
                        ["[" d1: test-source-rule d2: "]" ] (type: in types 'tst)
                            |
                        end (type: in types 'end) break
                            |
                        position: single-value d2: [
                            if (issue? get 'value) (type: in types 'flg)
                            |
                            if (file? get 'value) (type: in types 'fil)
                        ]
                    ]
                    t2: (
                        if blank? type [
                            fail [
                                {Scanning failed at line/col} text-location-of t1
                                {near} mold copy/part to string! t1 80
                            ]
                        ]
                        do-scanned
                    )
                    :t2 ; do-scanned may have changed position of t2.
                ]
            ]

            not-eolc: complement charset crlf

            scan: procedure [
                {Scan the contents for basic tokens.}
                contents [binary!]
            ] [
                assert [parse contents rule]
            ]
        ]
    ]

    make-original-tests-tokeniser: function [
        {Modifies basic syntax tokens from scanner into final tokens.}
    ][
        context [

            num: ids: _
            note: _
            test: code: pattern: txt: _
            issue-id: ["bug#" copy num some digit]
            position: _

            scanner: make-original-tests-scanner

            ttype: tvalue: _
            do-token: _

            ; Customise scanner, to reinterpret tokens.
            do in scanner [

                types/tst: [
                    ; We want to extract issues encoded as ISSUE! from inside the test.
                    test: copy/part d1 d2
                    if attempt [
                        all [
                            issue? num: first code: load test
                            parse next form first code [some digit]
                            num = do/next code 'position
                            position = next code
                        ]
                    ][
                        ; Rewrite test to remove issue.
                        pattern: compose [(mold num) opt [any whitespace #"|"] any whitespace]
                        parse test [any whitespace remove pattern]
                        ttype: 'isu
                        tvalue: compose/deep [[(num)] ""]
                        do-token
                    ]

                    ttype: type
                    tvalue: test
                    do-token
                ]

                types/flg: [
                    ttype: type
                    tvalue: next mold value
                    do-token
                ]

                types/cmt: [

                    ; Extract issues encoded within comments.
                    txt: trim/head/tail to string! copy/part d1 d2
                    if find txt "#" [
                        ids: copy []
                        if parse txt [
                            opt #"#" issue-id (append ids num)
                            any [
                                {, } issue-id (append ids num)
                            ]
                            opt [{ - } | [":" | some "."] some whitespace]
                            copy note to end
                            |
                            copy note some [not [[{, } | {: }] issue-id] skip]
                            opt {, } issue-id opt #"." (append ids num)
                        ] [
                            ttype: 'isu
                            tvalue: compose/only [(ids) (note)]
                            do-token
                        ] else [
                            ttype: type
                            tvalue: copy/part d1 d2
                            do-token
                        ]
                    ] else [
                        ttype: type
                        tvalue: copy/part d1 d2
                        do-token
                    ]
                ]

                types/eol: [
                    ttype: type
                    tvalue: eol
                    do-token
                ]

                types/fil: types/wsp: [
                    ttype: type
                    tvalue: copy/part t1 t2
                    do-token
                ]

                types/end: _

                do-scanned: does [do get type]
            ]
        ]
    ]

    tokenise-original-tests: function [
        {Returns block of tokens.}
        test-source [object!]
    ][
        do in make-original-tests-tokeniser [
            collect [
                do-token: does [keep ttype keep/only tvalue]
                scanner/scan test-source/contents
            ]
        ]
    ]

    test-renderer: context [
        output: copy #{}
        issues: copy []
        eol: newline
        last-emit: _

        emit: func [value][
            append output last-emit: value
        ]

        render: function [tokens][
            clear output
            parse tokens [thru 'eol set eol [string! | char!]]
            for-each [type val] tokens [
                switch/default type [
                    eol wsp [emit val]
                    cmt [emit #";" emit val emit eol]
                    tst [
                        replace/all val newline eol
                        if empty? issues [
                            if eol <> last-emit [emit eol]
                            emit #"(" emit val emit #")"
                        ] else [
                            if eol <> last-emit [emit eol]
                            emit "["
                            state: #issues-only
                            for-each issue issues [
                                if state = #issues-only [
                                    if not empty? issue/2 [
                                        state: #issues-and-note
                                    ]
                                ] else [
                                    if not empty? issue/2 [
                                        state: #issue-lines
                                    ]
                                ]
                            ]
                            num: 0
                            for-each issue issues [
                                if find [#issues-only #issues-and-note] state [
                                    ; Append issue.
                                    if 1 < (num: me + 1) [
                                        emit space
                                    ]
                                ] else [
                                    emit eol emit {    }
                                ]
                                emit spaced map-each id issue/1 [to issue! id]
                                if not empty? note: issue/2 [
                                    emit spaced [{ ;} note]
                                ]
                            ]
                            clear issues

                            if parse val [to newline to end] [
                                if state = #issues-only [
                                    state: #multiline-test
                                    emit " (" emit val
                                ] else [
                                    state: #multiline-indented
                                    emit eol
                                    emit "    ("
                                    emit val
                                    emit "    "
                                ]
                            ] else [
                                state: #oneline-test
                                emit eol
                                emit "    ("
                                emit val
                            ]
                            emit #")"
                            if state <> #multiline-test [
                                emit eol
                            ]
                            emit #"]"
                        ]
                    ]
                    fil [emit val]
                    flg [emit #"<" emit val emit #">"]
                    isu [append/only issues val]
                    end []
                ] [
                    fail [{Cannot render type } (type)]
                ]
            ]

            lock copy output
        ]
    ]

    files-upgraded: copy []

    upgrade-test-source: function [
        {Converts from original format to new convention.}
        test-source [object!]
    ][
        tokens: tokenise-original-tests test-source
        for i 2 length of tokens 2 [
            if not block? tk: pick tokens i [
                poke tokens i to string! pick tokens i
            ]
        ]

        test-source/contents: test-renderer/render tokens

        comment [
            ;; replace test-source/filepath {/C/Projects/Public/ren-c/tests} {/C/Projects/Public/temp.201803-test-format/tests}
            folder: first split-path test-source/filepath
            if not exists? folder [
                make-dir/deep folder
            ]
            write test-source/filepath test-source/contents
            append files-upgraded test-source/filepath
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
        ;; upgrade-test-source test-source ; TODO: Remove when not needed.
        test-source
    ]

    ; End temporary code section.
    ; -----------------------------------------------------------------------

    set 'collect-tests procedure [
        collected-tests [block!]
            {collect the tests here (modified)}
        test-file [file!]
    ][
        current-dir: what-dir
        print ["file:" mold test-file]

        either error? err: try [
            if file? test-file [
                test-file: clean-path test-file
                change-dir first split-path test-file
            ]
            test-sources: get in load-testfile test-file 'contents
        ][
            ; probe err
            append collected-tests reduce [
                test-file 'dialect {^/"failed, cannot read the file"^/}
            ]
            change-dir current-dir
            leave
        ][
            change-dir current-dir
            append collected-tests test-file
        ]

        types: context [
            wsp: cmt: val: tst: grpb: grpe: flg: fil: isu: str: end: _
        ]

        flags: copy []

        wsp: [
            [
                some whitespace (type: in types 'wsp)
                |
                ";" [thru newline | to end] (type: in types 'cmt)
            ]
        ]

        any-wsp: [any [wsp emit-token]]

        single-value: parsing-at x [
            if not error? try [
                set [value: next-position:] transcode/next x
            ][
                type: in types 'val
                next-position
            ]
        ]

        single-test: [
            copy vector ["(" test-source-rule ")"] (
                type: in types 'tst
                append/only collected-tests flags
                append collected-tests vector
            )
        ]

        grouped-tests: [
            "[" (type: in types 'grpb) emit-token
            any [any-wsp single-value if (issue? value) (type: in types 'isu) emit-token]
            opt [any-wsp single-value if (string? value) (type: in types 'str) emit-token]
            any [any-wsp single-test emit-token]
            any-wsp "]" (type: in types 'grpe) emit-token
        ]

        token: [
            position: (type: value: _)
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
                if (tag? get 'value) (
                    type: in types 'flg
                    append flags value
                )
                |
                if (file? get 'value) (
                    type: in types 'fil
                    collect-tests collected-tests value
                    print ["file:" mold test-file]
                    append collected-tests test-file
                )
            ]
        ]

        emit-token: [
            token-end:
;;            (prin "emit: " probe compose [(type) (to string! copy/part position token-end)])
            position: (type: value: _)
        ]

        rule: [any token]

        unless parse test-sources rule [
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
        if error? try [log-contents: read log-file] [
            fail ["Unable to read " mold log-file]
        ]

        parse log-contents [
            (guard: [end skip])
            any [
                any whitespace
                [
                    position: "%"
                    (set [value: next-position:] transcode/next position)
                    :next-position
                        |
                    ; dialect failure?
                    some whitespace
                    {"} thru {"}
                        |
                    copy last-vector ["(" test-source-rule ")"]
                    any whitespace
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
                    "system/version:" to end (guard: _)
                        |
                    (fail "collect-logs - log file parsing problem")
                ] position: guard break ; Break when error detected.
                    |
                :position
            ]
        ]
    ]
]
