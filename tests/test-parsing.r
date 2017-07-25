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

whitespace: charset [#"^A" - #" " "^(7F)^(A0)"]


read-binary: :read

make object! [

    position: _
    success: _
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

    set 'collect-tests procedure [
        collected-tests [block!]
            {collect the tests here (modified)}
        test-file [file!]
    ][
        current-dir: what-dir
        print ["file:" mold test-file]

        either error? try [
            if file? test-file [
                test-file: clean-path test-file
                change-dir first split-path test-file
            ]
            test-sources: read test-file
        ][
            append collected-tests reduce [
                test-file 'dialect {^/"failed, cannot read the file"^/}
            ]
            change-dir current-dir
            leave
        ][
            change-dir current-dir
            append collected-tests test-file
        ]

        flags: copy []
        rule: [
            any [
                some whitespace
                    |
                ";" [thru newline | to end]
                    |
                copy vector ["[" test-source-rule "]"] (
                    append/only collected-tests flags
                    append collected-tests vector
                    flags: copy []
                )
                    |
                end break
                    |
                position: (
                    case [
                        any [
                            error? try [
                                set [value: next-position:] (
                                    transcode/next position
                                )
                            ]
                            blank? next-position
                        ][
                            stop: [:position]
                        ]

                        issue? get 'value [
                            append flags value
                            stop: [end skip]
                        ]

                        file? get 'value [
                            collect-tests collected-tests value
                            print ["file:" mold test-file]
                            append collected-tests test-file
                            stop: [end skip]
                        ]
                    ] else [
                        stop: [:position]
                    ]
                ) stop break
                    |
                :next-position
            ]
        ]

        unless parse test-sources rule [
            append collected-tests reduce [
                'dialect
                unspaced [
                    newline
                    {"failed, line:} space line-number? position {"} newline
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
            (stop: [end skip])
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
                    copy last-vector ["[" test-source-rule "]"]
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
                    "system/version:" to end (stop: _)
                        |
                    (fail "log file parsing problem")
                ] position: stop break
                    |
                :position
            ]
        ]
    ]
]
