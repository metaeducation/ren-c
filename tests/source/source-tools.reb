REBOL [
    Title: "Rebol 'Lint'-style Checking Tool for source code invariants"
    Rights: {
        Copyright 2015 Brett Handley
        Copyright 2015-2018 Rebol Open Source Contributors
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This tool arose from wanting to use Rebol for a pre-commit hook:

        https://codeinthehole.com/tips/tips-for-using-a-git-pre-commit-hook/

        It can scan for simple things like inconsistent CR LF line endings, or
        more complex policies for the codebase.  Since a C tokenizer using
        PARSE rules had already been created for auomatically generating
        header files for the API, that tokenizer is used here to give some
        level of "C syntax awareness".

        (Note: Since that C parser is used in bootstrap, not all cutting edge
        features can be used in it...since it must build with older Rebols.)

        Some of the checks are fully enforced, such as that lines not end in
        stray whitespace...or that the names in comment blocks are actually in
        sync with the corresponding C identifiers.  Other rule violations are
        just given as warnings, but not formally registered as failures yet
        (such as when lines of code are longer than 80 columns long).

        It is a baseline for implementing more experiments, and by using
        Rebol code for the checks it also exercises more code paths.
    }
]

; Root folder of the repository.
; This script makes some assumptions about the structure of the repo.
;

parse2: :parse/redbol  ; uses Redbol parsing conventions in common-parsers.r

do <../../tools/common.r>

do join tools-dir %common-parsers.r

do join tools-dir %text-lines.reb
do join tools-dir %read-deep.reb

null-to-blank: func [x [any-value!]] [either null? x [_] [:x]]

; rebsource is organised along the lines of a context sensitive vocabulary.
;

rebsource: context [

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

    ; Source paths are recursively read.
    ;
    source-paths: [
        %src/
        %tests/
    ]

    extensions: [
        %.c c
        %.r rebol
        %.reb rebol
    ]

    whitelisted: [
        %src/core/u-bmp.c
        %src/core/u-compress.c
        %src/core/u-gif.c
        %src/core/u-jpg.c
        %src/core/u-md5.c
        %src/core/u-png.c
        %src/core/u-sha1.c
        %src/core/u-zlib.c
    ] ; Not analysed ...


    log-emit: function [
        {Append a COMPOSE'd block to a log block, clearing any new-line flags}

        return: [~]
        log [block!]
        label [tag!]
        body [block!]
    ][
        body: new-line/all compose/only body false
        append/line log (head insert body label)
    ]

    analyse: context [

        files: function [
            {Analyse the source files of REBOL.}
            return: [block!]
        ][
            collect [
                for-each source list/source-files [
                    if find whitelisted source [continue]

                    keep analyse/file source
                ]
            ]
        ]

        file: function [
            {Analyse a file returning facts.}
            return: [~null~ block!]
            file
        ][
            all [
                filetype: select extensions extension-of file
                type: in source filetype
                reeval (ensure action! get type) file (read join repo-dir file)
            ]
        ]

        source: context [

            c: function [
                {Analyse a C file at the C preprocessing token level}

                return: [block!]
                    "Facts about the file (lines that are too long, etc.)"
                file [file!]
                data [binary!]
            ][
                analysis: analyse/text file data
                emit: specialize 'log-emit [log: analysis]

                data: as text! data

                identifier: c-lexical/grammar/identifier
                c-pp-token: c-lexical/grammar/c-pp-token

                malloc-found: copy []

                malloc-check: [
                    and identifier "malloc" (
                        append malloc-found reify text-line-of position
                    )
                ]

                parse2/case data [
                    some [
                        position:  ; <here>
                        malloc-check
                        | c-pp-token
                    ]
                ]

                if not empty? malloc-found [
                    emit <malloc> [(file) (malloc-found)]
                ]

                all [
                    not tail? data
                    not equal? newline last data
                ] then [
                    emit <eof-eol-missing> [(file)]
                ]

                emit-proto: function [return: [~] proto] [
                    if not block? proto-parser/data [return]

                    eval in c-parser-extension [
                        if last-func-end [
                            all [
                                parse2/match last-func-end [
                                    function-spacing-rule
                                    position:  ; <here>
                                    to end
                                ]
                                same? position proto-parser/parse-position
                            ] else [
                                line: (
                                    text-line-of proto-parser/parse-position
                                )
                                append
                                    non-std-func-space: default [copy []]
                                    reify line
                            ]
                        ]
                    ]

                    parse2/match proto-parser/data [
                        opt 'export
                        set name: set-word! (name: to-word name)
                        opt 'infix
                        ['native | ahead path! into ['native to end]]
                        to end
                    ] then [
                        ;
                        ; It's a `some-name?: native [...]`, so we expect
                        ; `DECLARE_NATIVE(SOME_NAME_Q)` to be correctly lined up
                        ; as the "to-c-name" of the Rebol set-word
                        ;
                        if proto-parser/proto-arg-1 <> to-c-name name [
                            line: text-line-of proto-parser/parse-position
                            emit <id-mismatch> [
                                (mold proto-parser/data/1) (file) (reify line)
                            ]
                        ]
                    ] else [
                        ;
                        ; ... ? (not a native)
                        ;
                        any [
                            proto-parser/proto-id =
                                <- (form to word! proto-parser/data/1)
                            proto-parser/proto-id =
                                <- unspaced [
                                    "API_" to word! proto-parser/data/1
                                ]
                        ] else [
                            line: text-line-of proto-parser/parse-position
                            emit <id-mismatch> [
                                (mold proto-parser/data/1) (file) (reify line)
                            ]
                        ]
                    ]
                ]

                non-std-func-space: null
                proto-parser/emit-proto: :emit-proto
                proto-parser/process data

                if non-std-func-space [
                    emit <non-std-func-space> [(file) (non-std-func-space)]
                ]

                analysis
            ]

            rebol: function [
                {Analyse a Rebol file (no checks beyond those for text yet)}

                return: [block!]
                    "Facts about the file (end of line whitespace, etc.)"
                file [file!]
                data
            ][
                analysis: analyse/text file data
                analysis
            ]
        ]

        text: function [
            {Analyse textual formatting irrespective of language}

            return: [block!]
                "Facts about the text file (inconsistent line endings, etc)"
            file [file!]
            data
        ][
            analysis: copy []
            emit: specialize 'log-emit [log: analysis]

            data: read join repo-dir file

            bol: null
            line: null

            stop-char: charset { ^-^M^/}
            ws-char: charset { ^-}
            wsp: [some ws-char]

            eol: [line-ending | alt-ending (append inconsistent-eol line)]
            line-ending: null

            ;
            ; Identify line termination.

            all [
                position: find data #{0a}
                1 < index of position
                13 = first back position
            ] then [
                line-ending: unspaced [CR LF]
                alt-ending: LF
            ] else [
                line-ending: LF
                alt-ending: unspaced [CR LF]
            ]

            count-line: [
                (
                    line-len: subtract index of position index of bol
                    if line-len > standard/std-line-length [
                        append over-std-len line
                        if line-len > standard/max-line-length [
                            append over-max-len line
                        ]
                    ]
                    line: 1 + line
                )
                bol: <here>
            ]

            tabbed: copy []
            whitespace-at-eol: copy []
            over-std-len: copy []
            over-max-len: copy []
            inconsistent-eol: copy []

            parse/case data [

                last-pos: <here>

                opt [
                    bol: <here>
                    one (line: 1)
                    seek bol
                ]

                opt some [
                    to stop-char
                    position: <here>
                    [
                        eol count-line
                        | #"^-" (append tabbed line)
                        | wsp ahead [line-ending | alt-ending] (
                            append whitespace-at-eol line
                        )
                        | one
                    ]
                ]
                position: <here>

                to <end>
            ]

            if not empty? over-std-len [
                emit <line-exceeds> [
                    (standard/std-line-length) (file) (over-std-len)
                ]
            ]

            if not empty? over-max-len [
                emit <line-exceeds> [
                    (standard/max-line-length) (file) (over-max-len)
                ]
            ]

            for-each list [tabbed whitespace-at-eol] [
                if not empty? get list [
                    emit as tag! list [(file) (get list)]
                ]
            ]

            if not empty? inconsistent-eol [
                emit <inconsistent-eol> [(file) (inconsistent-eol)]
            ]

            all [
                not tail? data
                not equal? 10 last data ; Check for newline.
            ] then [
                emit <eof-eol-missing> [
                     (file) (reduce [reify text-line-of tail of to text! data])
                ]
            ]

            analysis
        ]
    ]

    list: context [

        source-files: function [
            {Retrieves a list of source files (relative paths).}
        ][
            if not repo-dir [fail {Configuration of repo-dir required.}]

            files: read-deep/full/strategy source-paths :source-files-seq

            sort files
            new-line/all files true

            files
        ]

        source-files-seq: function [
            {Take next file from a sequence that is represented by a queue.}
            return: [~null~ file!]
            queue [block!]
        ][
            item: ensure file! take queue

            if equal? #"/" last item [
                contents: read join repo-dir item
                insert queue map-each x contents [join item x]
                item: null
            ] else [
                split-path/file item the filename:
                any [
                    parse/match filename ["tmp-" to <end>]
                    not find extensions extension-of item
                ] then [
                    item: null
                ]
            ]

            item ;-- blanked items are to be filtered out
        ]
    ]

    c-parser-extension: context bind bind [

        ; Extend parser to support checking of function spacing.

        last-func-end: null

        lbrace: [and punctuator "{"]
        rbrace: [and punctuator "}"]
        braced: [lbrace opt some [braced | not rbrace skip] rbrace]

        function-spacing-rule: (
            bind/copy standard/function-spacing c-lexical/grammar
        )

        grammar/function-body: braced

        append grammar/format-func-section [
            last-func-end:
            opt some [nl | eol | wsp]
        ]

        append/only grammar/other-segment the (
            last-func-end: null
        )

    ] proto-parser c-lexical/grammar

    extension-of: function [
        {Return file extension for file.}
        return: [file!]
        file [file!]
    ][
        find/last file "." else [copy %""]
    ]
]
