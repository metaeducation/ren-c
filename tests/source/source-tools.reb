REBOL [
    Title: "Rebol 'Lint'-style Checking Tool for source code invariants"
    Type: module
    Name: Source-Tools
    Rights: --{
        Copyright 2015 Brett Handley
        Copyright 2015-2021 Ren-C Open Source Contributors
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Purpose: --{
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
    }--
]

; Root folder of the repository.
; This script makes some assumptions about the structure of the repo.
;

import %../../tools/common.r  ; sets REPO-DIR (among other things)

c-lexical: import %% (repo-dir)/tools/c-lexicals.r
import %% (repo-dir)/tools/common-parsers.r
import %% (repo-dir)/tools/text-lines.reb
import %% (repo-dir)/tools/read-deep.reb

; rebsource is organised along the lines of a context sensitive vocabulary.
;

/logfn: func [message][
    message: compose message
    new-line:all message 'no
    print mold message
]
/log: logfn/

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
    function-spacing: [repeat 3 eol]
]

; Source paths are recursively read.
;
source-paths: [
    %src/
    %tests/
    %extensions/
]

extensions: [
    %.c c
    %.r rebol
    %.reb rebol
]

; Third party files don't obey Rebol source rules, so don't bother to
; check them.
;
; !!! Should be sure whitelisted files actually exist by trying to READ
; them...this list had gotten stale.
;
whitelisted: [
    %src/core/u-zlib.c
    %src/core/f-dtoa.c

; IMAGE! currently not part of the project while type system is worked on
;
;    %extensions/bmp/mod-bmp.c
;    %extensions/gif/mod-gif.c
;    %extensions/jpg/u-jpg.c
;    %extensions/png/lodepng.h
;    %extensions/png/lodepng.c

    %extensions/crypt/mbedtls/

    %extensions/filesystem/libuv/
]


/log-emit: func [
    "Append a COMPOSE'd block to a log block, clearing any new-line flags"

    return: [~]
    log [block!]
    label [tag!]
    body [block!]
][
    body: compose body
    new-line:all body 'no
    append:line log spread (head of insert body label)
]

export analyse: context [

    /files: func [
        "Analyse the source files of Rebol"
        return: [block!]
    ][
        return collect [
            for-each 'source list/source-files [
                if find whitelisted source [continue]

                keep maybe spread analyse/file source
            ]
        ]
    ]

    /file: func [
        "Analyse a file returning facts"
        return: [~null~ block!]
        file
    ][
        lib/print ["Analyzing:" file]  ; subvert tests PRINT disablement
        return all [
            let filetype: select extensions extension-of file
            let type: has source filetype
            (reeval (ensure action! get type) file
                (read %% (repo-dir)/(file)))
        ]
    ]

    source: context [

        /c: func [
            "Analyse a C file at the C preprocessing token level"

            return: "Facts about the file (lines that are too long, etc)"
                [block!]
            file [file!]
            data [blob!]
            <local> position  ; used sketchily in rules, no LET in parse :-/
        ][
            let analysis: analyse/text file data
            let /emit: specialize log-emit/ [log: analysis]

            data: as text! data

            let identifier: c-lexical.grammar.identifier
            let c-pp-token: c-lexical.grammar.c-pp-token

            let malloc-found: copy []

            let malloc-check: [
                ahead identifier "malloc" (
                    append malloc-found try text-line-of position
                )
            ]

            parse3:case data [
                some [
                    position: <here>
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

            let non-std-func-space: null

            let /emit-proto: func [return: [~] proto] [
                if not block? proto-parser.data [return ~]

                eval overbind c-parser-extension [
                    if last-func-end [
                        all [
                            parse3 last-func-end [
                                function-spacing-rule
                                position: <here>
                                accept (~)
                                |
                                accept (null)
                            ]
                            same? position proto-parser.parse-position
                        ] else [
                            let line: text-line-of proto-parser.parse-position
                            append
                                non-std-func-space: default [copy []]
                                line  ; should it be appending BLANK! ?
                        ]
                    ]
                ]

                let name: ~
                if parse proto-parser.data [
                    opt 'export
                    name: ['//: | set-word?/] (name: resolve name)
                    opt ['infix | 'infix:defer | 'infix:postpone]
                    [
                        'native
                        | 'native:combinator
                        | 'native:generic
                        | 'native:intrinsic
                    ]
                    accept (okay)
                    |
                    accept (null)
                ] [
                    ;
                    ; It's a `some-name?: native [...]`, so we expect
                    ; `DECLARE_NATIVE(SOME_NAME_Q)` to be correctly lined up
                    ; as the "to-c-name" of the Rebol set-word
                    ;
                    if (
                        proto-parser.proto-arg-1
                        <> uppercase to-c-name:scope name #prefixed
                    )[
                        let line: text-line-of proto-parser.parse-position
                        emit <id-mismatch> [
                            (name) (file) (line)
                        ]
                    ]
                ] else [
                    ;
                    ; ... ? (not a native)
                    ;
                    if not set-word? proto-parser.data.1 [
                        fail ["UNRECOGNIZED PROTO:" mold proto-parser.data]
                    ]
                    any [
                        (proto-parser.proto-id =
                            form unchain proto-parser.data.1)
                        (proto-parser.proto-id =
                            unspaced [
                                "API_" form unchain proto-parser.data.1
                            ])
                    ] else [
                        let line: text-line-of proto-parser.parse-position
                        emit <id-mismatch> [
                            (mold proto-parser.data.1) (file) (line)
                        ]
                    ]
                ]
            ]

            /proto-parser.emit-proto: emit-proto/
            proto-parser/process data

            if non-std-func-space [
                emit <non-std-func-space> [(file) (non-std-func-space)]
            ]

            return analysis
        ]

        /rebol: func [
            "Analyse a Rebol file (no checks beyond those for text yet)"

            return: [block!]
                "Facts about the file (end of line whitespace, etc.)"
            file [file!]
            data
        ][
            let analysis: analyse/text file data
            return analysis
        ]
    ]

    /text: func [
        "Analyse textual formatting irrespective of language"

        return: [block!]
            "Facts about the text file (inconsistent line endings, etc)"
        file [file!]
        data
        <local> position last-pos line-ending alt-ending  ; no PARSE let :-/
    ][
        let analysis: copy []
        let /emit: specialize log-emit/ [log: analysis]

        data: read %% (repo-dir)/(file)

        let bol: null  ; beginning of line
        let line: null

        let stop-char: charset -{ ^-^M^/}-
        let ws-char: charset -{ ^-}-
        let wsp: [some ws-char]

        let eol: [line-ending | alt-ending (append inconsistent-eol line)]
        line-ending: null
        alt-ending: null

        ;
        ; Identify line termination.

        all [
            position: find data #{0a}
            1 < index of position
            13 = first back position
        ] also [
            line-ending: unspaced [CR LF]
            alt-ending: LF
        ] else [
            line-ending: LF
            alt-ending: unspaced [CR LF]
        ]

        let over-std-len: copy []
        let over-max-len: copy []

        let count-line: [
            (
                let line-len: subtract index of position index of bol
                if line-len > standard.std-line-length [
                    append over-std-len line
                    if line-len > standard.max-line-length [
                        append over-max-len line
                    ]
                ]
                line: 1 + line
            )
            bol: <here>
        ]

        let tabbed: copy []
        let whitespace-at-eol: copy []
        let inconsistent-eol: copy []

        parse3:case data [
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
                (standard.std-line-length) (file) (over-std-len)
            ]
        ]

        if not empty? over-max-len [
            emit <line-exceeds> [
                (standard.max-line-length) (file) (over-max-len)
            ]
        ]

        for-each 'list reduce [$tabbed $whitespace-at-eol] [
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
                    (file) (reduce [text-line-of tail of to text! data])
            ]
        ]

        return analysis
    ]
]

list: context [

    /source-files: func [
        "Retrieves a list of source files (relative paths)"
    ][
        let files: read-deep:full:strategy source-paths source-files-seq/

        sort files
        new-line:all files 'yes

        return files
    ]

    /source-files-seq: func [
        "Take next file from a sequence that is represented by a queue"
        return: [~null~ file!]
        queue [block!]
    ][
        let item: ensure file! take queue

        if find whitelisted item [
            return null
        ]

        if equal? #"/" last item [
            let contents: read %% (repo-dir)/(item)
            insert queue spread map-each 'x contents [join item x]
            item: null
        ] else [
            any [
                try parse [# {#}]: lib/split-path item ["tmp-" ...]
                not find extensions extension-of item
            ] then [
                item: null
            ]
        ]

        return item  ; nulled items are to be filtered out
    ]
]

c-parser-extension: context bind c-lexical.grammar bind proto-parser [

    ; Extend parser to support checking of function spacing.

    last-func-end: null

    lbrace: [ahead punctuator "{"]
    rbrace: [ahead punctuator "}"]
    braced: [lbrace opt some [braced | not ahead rbrace one] rbrace]

    function-spacing-rule: (
        bind c-lexical.grammar standard.function-spacing
    )

    grammar.function-body: braced

    append grammar.format-func-section [  ; spread loses binding
        last-func-end: <here>
        opt some [nl | eol | wsp]
    ]

    append grammar.other-segment $(
        last-func-end: null
    )
]

/extension-of: func [
    "Return file extension for file"
    return: [file!]
    file [file!]
][
    return find-last file "." else [copy %""]
]
