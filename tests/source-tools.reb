REBOL [
    Title: "Rebol C Source Tools"
    Rights: {
        Copyright 2015 Brett Handley
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Brett Handley"
    Purpose: {Process Rebol C source.}
]

ren-c-repo: any [
    if exists? %../src/tools/ [%../]
    if exists? %../ren-c/src/tools/ [%../ren-c/]
]

ren-c-repo: clean-path ren-c-repo

do ren-c-repo/src/tools/r2r3-future.r
do ren-c-repo/src/tools/common.r
do ren-c-repo/src/tools/common-parsers.r
do %lib/text-lines.reb


rebsource: context [

    src-folder: clean-path ren-c-repo/(%src/)
    ; Path to src/

    logfn: func [message][print mold new-line/all compose/only message false]
    log: :logfn

    standard: context [
        ;
        ; Not counting newline, lines should be no longer than this.
        ;
        std-line-length: 79

        ; Not counting newline, lines over this length have an extra warning.
        ;
        max-line-length: 127

        ; Parse Rule which specifies the standard spacing between functions,
        ; from final right brace of leading function
        ; to intro comment of following function.
        ;
        function-spacing: [3 eol]
    ]

    fixed-source-paths: [
        %core/
        %os/
        %os/generic/
        %os/linux/
        %os/posix/
        %os/windows/
    ]

    whitelisted: [
        %core/u-bmp.c
        %core/u-compress.c
        %core/u-gif.c
        %core/u-jpg.c
        %core/u-md5.c
        %core/u-png.c
        %core/u-sha1.c
        %core/u-zlib.c
    ] ; Not analysed ...


    analyse: context [

        files: function [
            {Analyse the source files of REBOL.}
        ][
            file-list: list/c-files

            files-analysis: make block! []

            for-each filepath file-list [
                analysis: analyse/file filepath
                append files-analysis analysis
            ]

            files-analysis
        ]

        file: function [
            {Analyse a file returning facts.}
            file
        ][
            if whitelisted? file [return blank]

            analysis: make block! []

            emit: function [body][
                insert position: tail analysis compose/only body
                new-line position true
            ]

            add-line: function [list][
                if blank? buffer: get list [
                    set list buffer: copy []
                ]
                append buffer line-of file-text position
            ]

            text: read/string src-folder/:file

            all [
                non-std-lines: lines-exceeding standard/std-line-length text
                    |
                emit [
                    line-exceeds
                    (standard/std-line-length) (file) (non-std-lines)
                ]
            ]

            all [
                overlength-lines: lines-exceeding standard/max-line-length text
                    |
                emit [
                    line-exceeds
                    (standard/max-line-length) (file) (overlength-lines)
                ]
            ]

            wsp-not-eol: exclude c.lexical/charsets/ws-char charset {^/}
            wsp-not-tabeol: exclude wsp-not-eol charset {^-}

            eol-wsp: malloc: tabbed: _
            file-text: text

            do bind [

                is-identifier: [and identifier]

                wsp-eol: [some wsp-not-eol eol]

                trimmed-comment: [{//} some [not eol not wsp-eol skip]]

                eol-wsp-check: [
                    wsp-eol (add-line 'eol-wsp)
                ]

                malloc-check: [
                    is-identifier "malloc" (add-line 'malloc)
                ]

                tab-check: [
                    #"^-" (add-line 'tabbed)
                ]

                parse/case file-text [
                    some [
                        position:
                        malloc-check
                        | trimmed-comment
                        | eol-wsp-check
                        | some wsp-not-tabeol | tab-check
                        | c-pp-token
                    ]
                ]

            ] c.lexical/grammar

            foreach list [eol-wsp malloc tabbed][
                if not blank? get list [
                    emit [(list) (file) (get list)]
                ]
            ]

            if all [
                not tail? file-text
                not equal? newline last file-text
            ][
                emit [eof-eol-missing (file)]
            ]

            emit-proto: procedure [proto][
                if all [
                    'format2015 = proto-parser/style
                    block? proto-parser/data
                ][
                    do bind [
                        if last-func-end [
                            if not all [
                                parse last-func-end [
                                    function-spacing-rule
                                    position:
                                    to end
                                ]
                                same? position proto-parser/parse.position
                            ][
                                line: line-of text proto-parser/parse.position
                                append any [
                                    non-std-func-space
                                    set 'non-std-func-space copy []
                                ] line-of file-text proto-parser/parse.position
                            ]
                        ]
                    ] parser-extension

                    either find/match mold proto-parser/data/2 {native} [
                        ;
                        ; It's a `some-name?: native [...]`, so we expect
                        ; `REBNATIVE(some_name_q)` to be correctly lined up
                        ; as the "to-c-name" of the Rebol set-word
                        ;
                        unless (
                            equal?
                                proto-parser/proto.arg.1
                                (to-c-name to word! proto-parser/data/1)
                        ) [
                            line: line-of text proto-parser/parse.position
                            emit [
                                id-mismatch
                                (mold proto-parser/data/1) (file) (line)
                            ]
                        ]
                    ][
                        ;
                        ; ... ? (not a native)
                        ;
                        unless (
                            equal?
                                proto-parser/proto.id
                                form to word! proto-parser/data/1
                        ) [
                            line: line-of text proto-parser/parse.position
                            emit [
                                id-mismatch
                                (mold proto-parser/data/1) (file) (line)
                            ]
                        ]
                    ]
                ]

            ]

            non-std-func-space: _
            proto-parser/emit-proto: :emit-proto
            proto-parser/process text

            if non-std-func-space [
                emit [non-std-func-space (file) (non-std-func-space)]
            ]

            analysis
        ]
    ]

    list: context [

        c-files: function [
            {Retrieves a list of .c scripts (relative paths).}
        ][
            if not src-folder [fail {Configuration required.}]

            files: make block! []
            for-each path fixed-source-paths [
                for-each file read join src-folder path [
                    append files join path file
                ]
            ]

            remove-each file files [not parse file [thru {.c}]]
            sort files

            files
        ]
    ]

    parser-extension: context bind bind [

        ; Extend parser to support checking of function spacing.

        last-func-end: _

        lbrace: [and punctuator #"{"]
        rbrace: [and punctuator #"}"]
        braced: [lbrace any [braced | not rbrace skip] rbrace]

        function-spacing-rule: (
            bind/copy standard/function-spacing c.lexical/grammar
        )

        grammar/function-body: braced

        append grammar/format2015-func-section [
            last-func-end:
            any [nl | eol | wsp]
        ]

        append/only grammar/other-segment to group! [
            last-func-end: _
        ]

    ] proto-parser c.lexical/grammar

    whitelisted?: function [
        {Returns true if file should not be analysed.}
        file
    ][
        find? whitelisted file
    ]
]
