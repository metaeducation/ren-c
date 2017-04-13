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

    extensions: [
        %.c c
        %.r rebol
        %.reb rebol
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
            listing: list/source-files

            files-analysis: make block! []

            for-each source listing [
                if not whitelisted? source [
                    analysis: analyse/file source
                    if analysis [
                        append files-analysis analysis
                    ]
                ]
            ]

            files-analysis
        ]

        file: function [
            {Analyse a file returning facts.}
            return: [block! blank!]
            file
        ][
            all [
                filetype: filetype-of file
                to-value if type: in source filetype [
                    data: read src-folder/:file
                    evaluate: get type
                    evaluate file data
                ]
            ]
        ]

        source: context [

            c: function [
                {Analyse a C file returning facts.}
                file
                data
            ] [

                ;
                ; This analysis is at a token level (c preprocessing token).

                analysis: analyse/text file data

                data: to string! data

                identifier: c.lexical/grammar/identifier
                c-pp-token: c.lexical/grammar/c-pp-token

                malloc-found: make block! []

                malloc-check: [
                    and identifier "malloc" (append malloc-found line-of head position position)
                ]

                parse/case data [
                    some [
                        position:
                        malloc-check
                        | c-pp-token
                    ]
                ]

                if not empty? malloc-found [
                    emit analysis [malloc (file) (malloc-found)]
                ]

                if all [
                    not tail? data
                    not equal? newline last data
                ] [
                    emit analysis [eof-eol-missing (file)]
                ]

                emit-proto: procedure [proto] [
                    if all [
                        'format2015 = proto-parser/style
                        block? proto-parser/data
                    ] [
                        do bind [
                            if last-func-end [
                                if not all [
                                    parse last-func-end [
                                        function-spacing-rule
                                        position:
                                        to end
                                    ]
                                    same? position proto-parser/parse.position
                                ] [
                                    line: line-of data proto-parser/parse.position
                                    append any [
                                        non-std-func-space
                                        set 'non-std-func-space copy []
                                    ] line-of data proto-parser/parse.position
                                ]
                            ]
                        ] c-parser-extension

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
                                line: line-of data proto-parser/parse.position
                                emit analysis [
                                    id-mismatch
                                    (mold proto-parser/data/1) (file) (line)
                                ]
                            ]
                        ] [
                            ;
                            ; ... ? (not a native)
                            ;
                            unless (
                                equal?
                                proto-parser/proto.id
                                form to word! proto-parser/data/1
                            ) [
                                line: line-of data proto-parser/parse.position
                                emit analysis [
                                    id-mismatch
                                    (mold proto-parser/data/1) (file) (line)
                                ]
                            ]
                        ]
                    ]

                ]

                non-std-func-space: _
                proto-parser/emit-proto: :emit-proto
                proto-parser/process data

                if non-std-func-space [
                    emit analysis [non-std-func-space (file) (non-std-func-space)]
                ]

                analysis
            ]

            rebol: function [
                {Analyse a Rebol file returning facts.}
                file
                data
            ][
                analysis: analyse/text file data
                analysis
            ]
        ]

        text: function [
            {Analyse a source file returning facts.}
            file
            data
        ] [

            ;
            ; In this analysis we are interested in textual formatting irrespective of language.

            analysis: make block! []

            data: read src-folder/:file

            bol: _
            line: _

            stop-char: charset { ^-^M^/}
            ws-char: charset { ^-}
            wsp: [some ws-char]

            eol: [line-ending | alt-ending (append inconsistent-eol line)]
            line-ending: _

            ;
            ; Identify line termination.

            either all [
                position: find data #{0a}
                1 < index-of position
                13 = first back position
            ] [
                set [line-ending alt-ending] reduce [crlf newline]
            ][
                set [line-ending alt-ending] reduce [newline crlf]
            ]

            count-line: [
                (
                    line-len: subtract index-of position index-of bol 
                    if line-len > standard/std-line-length [
                        append over-std-len line
                        if line-len > standard/max-line-length [
                            append over-max-len line
                        ]
                    ]
                    line: 1 + line
                )
                bol:
            ]

            tabbed: make block! []
            eol-wsp: make block! []
            over-std-len: make block! []
            over-max-len: make block! []
            inconsistent-eol: make block! []

            parse/case data [

                last-pos:

                opt [bol: skip (line: 1) :bol]

                any [
                    to stop-char
                    position:
                    [
                        eol count-line
                        | #"^-" (append 'tabbed line)
                        | wsp and [line-ending | alt-ending] (append eol-wsp line)
                        | skip
                    ]
                ]
                position:

                to end
            ]

            if not empty? over-std-len [
                emit analysis [
                    line-exceeds
                    (standard/std-line-length) (file) (over-std-len)
                ]
            ]

            if not empty? over-max-len [
                emit analysis [
                    line-exceeds
                    (standard/max-line-length) (file) (over-max-len)
                ]
            ]

            foreach list [tabbed eol-wsp] [
                if not empty? get list [
                    emit analysis [(list) (file) (get list)]
                ]
            ]

            if not empty? inconsistent-eol [
                emit analysis [inconsistent-eol (file) (inconsistent-eol)]
            ]

            if all [
                not tail? data
                not equal? 10 last data ; Check for newline.
            ] [
                emit analysis [eof-eol-missing (file) (reduce [line-of data tail data])]
            ]

            analysis
        ]
    ]

    list: context [

        source-files: function [
            {Retrieves a list of source files (relative paths).}
        ][
            if not src-folder [fail {Configuration required.}]

            files: make block! 1 + (2 * length fixed-source-paths)

            for-each path fixed-source-paths [
                for-each file read join-of src-folder path [
                    if find extensions extension-of file [
                        append files join-of path file
                    ]
                ]
            ]

            sort files
            new-line/all files true

            files
        ]
    ]

    c-parser-extension: context bind bind [

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

    emit: function [log body] [
        insert position: tail log new-line/all compose/only body false
        new-line position true
    ]

    extension-of: function [
        {Return file extension for file.}
        file
    ][
        copy any [find/last file #"." {}]
    ]

    filetype-of: function [
        {Return filetype for file.}
        file
    ][
        to-value select extensions extension-of file
    ]

    whitelisted?: function [
        {Returns true if file should not be analysed.}
        file
    ][
        find? whitelisted file
    ]
]
