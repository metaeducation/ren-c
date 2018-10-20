REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Common Code for Emitting Text Files"
    Rights: {
        Copyright 2016-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        While emitting text files isn't exactly rocket science, it can help
        to have a few sanity checks on the process.

        The features added here vs. writing lines oneself are:

        * Some awareness of C constructs and the automatic conversion of
          Rebol symbols to valid C identifiers, as well as a not-too-invasive
          method for omitting commas from the ends of enum or initializer
          lists (they're not legal in standard C or C++).

        * Not allowing whitespace at the end of lines, tab characters in the
          output, some abilities to manage indentation.

        * Automatically generating C comment headers or Rebol headers, and
          including "DO NOT EDIT" warnings on temporary files.

        * Being able to use the information of the file and title being
          generated to provide notifications of what work is currently in
          progress to make errors easier to locate.
    }
]

cscape: function [
    {Escape Rebol expressions in templated C source, returns new string}

    return: "${} TO-C-NAME, $<> UNSPACED, $[]/$() DELIMIT closed/open"
        [text!]
    template "${Expr} case as-is, ${expr} lowercased, ${EXPR} is uppercased"
        [text!]
    /with "Lookup var words in additional context (besides user context)"
    context [any-word! any-context! block!]
][
    string: trim/auto copy template

    list: unique/case collect [
        parse string [(col: 0) any [
            [
                (dlm: _)

                "${" copy expr: to "}" skip (mode: #cname)
                    |
                "$<" copy expr: to ">" skip (mode: #unspaced)
                    |
                "$[" copy expr: to "]" skip (mode: #delim)
                copy dlm: to newline
                    |
                "$(" copy expr: to ")" skip (mode: #delim)
                copy dlm: remove to newline
            ] (
                keep/only compose [(col) (mode) (expr) (dlm)]
            )
                |
            newline (col: 0)
                |
            skip (col: col + 1)
        ]]
    ]

    if empty? list [return string]

    substitutions: collect [
        for-each item list [
            set [col: mode: expr: dlm:] item

            any-upper: did find/case expr charset [#"A" - #"Z"]
            any-lower: did find/case expr charset [#"a" - #"z"]
            keep expr

            code: load/all expr
            if with [
                context: compose [(context)] ;-- convert to block
                for-each item context [
                    bind code item
                ]
            ]
            sub: do code

            if blank? sub [sub: "/* _ */"] ;-- replaced in post-phase

            sub: switch mode [
                #cname [to-c-name sub]
                #unspaced [either block? sub [unspaced sub] [form sub]]
                #delim [delimit sub unspaced [dlm newline]]
                default [
                    fail ["Invalid CSCAPE mode:" mode]
                ]
            ]
            case [
                all [any-upper | not any-lower] [uppercase sub]
                all [any-lower | not any-upper] [lowercase sub]
            ]

            ; If the substitution started at a certain column, make any line
            ; breaks continue at the same column.
            ;
            indent: unspaced collect [keep newline | loop col [keep space]]
            replace/all sub newline indent

            keep sub
        ]
    ]

    string: reword/case/escape string substitutions ["${" "}"]
    string: reword/case/escape string substitutions ["$<" ">"]
    string: reword/case/escape string substitutions ["$(" ")"]
    string: reword/case/escape string substitutions ["$[" "]"]

    ; BLANK! in CSCAPE tries to be "smart" about omitting the item from its
    ; surrounding context, including removing lines when blank output and
    ; whitespace is all that ends up on them.  If the user doesn't want the
    ; intelligence, they should use "".
    ;
    parse string [
        (nonwhite: removed: false) start-line:
        while [
            space
            |
            newline
            [
                if (did all [not nonwhite | removed])
                :start-line remove thru [newline | end]
                |
                skip
            ]
            (nonwhite: removed: false) start-line:
            |
            remove "/* _ */" (removed: true) opt remove space
            |
            (nonwhite: true)
            skip
        ]
    ]

    return string
]


boot-version: load %../../src/boot/version.r

make-emitter: function [
    {Create a buffered output text file emitter}

    title "Title for the comment header (header matches file type)"
        [text!]
    file "Filename to be emitted... .r/.reb/.c/.h/.inc files supported"
        [file!]
    /temporary "DO-NOT-EDIT warning (automatic if file begins with 'tmp-')"

    <with>
    system ;-- The `System:` SET-WORD! below overrides the global for access
][
    if not by: system/script/header/file [
        fail [
            "File: should be set in the generating scripts header section"
            "so that generated files have a comment on what made them"
        ]
    ]

    print unspaced [{Generating "} title {" (via } by {)}]

    stem: second split-path file

    temporary: did any [temporary | parse stem ["tmp-" to end]]

    is-c: did parse stem [[thru ".c" | thru ".h" | thru ".inc"] end]

    is-js: did parse stem [thru ".js" end]

    e: make object! compose [
        ;
        ; NOTE: %make-headers.r directly manipulates the buffer, because it
        ; wishes to merge #ifdef/#endif cases
        ;
        ; !!! Should the allocation size be configurable?
        ;
        buf-emit: make text! 32000

        file: (file)
        title: (title)

        emit: function [
            {Write data to the emitter using CSCAPE templating (see HELP)}

            return: <void>
            :look [any-value! <...>]
            data [text! char! <...>]
        ][
            context: _
            firstlook: first look
            if any [
                lit-word? :firstlook
                block? :firstlook
                any-context? :firstlook
            ][
                context: take look
            ]

            data: take data
            switch type of data [
                text! [
                    adjoin buf-emit cscape/with data opt context
                ]
                char! [
                    adjoin buf-emit data
                ]
            ]
        ]


        write-emitted: function [
            return: <void>
            /tabbed
        ][
            if newline !== last buf-emit [
                probe skip (tail-of buf-emit) -100
                fail "WRITE-EMITTED needs NEWLINE as last character in buffer"
            ]

            if tab-pos: try find buf-emit tab [
                probe skip tab-pos -100
                fail "tab character passed to emit"
            ]

            if tabbed [
                replace/all buf-emit spaced-tab tab
            ]

            print [{WRITING =>} file]

            write-if-changed file buf-emit

            ; For clarity/simplicity, emitters are not reused.
            ;
            unset 'filename
            unset 'buf-emit
        ]
    ]

    either any [is-c is-js] [
        e/emit 'return {
            /**********************************************************************
            **
            **  REBOL [R3] Language Interpreter and Run-time Environment
            **  Copyright 2012 REBOL Technologies
            **  Copyright 2012-2018 Rebol Open Source Contributors
            **  REBOL is a trademark of REBOL Technologies
            **  Licensed under the Apache License, Version 2.0
            **
            ************************************************************************
            **
            **  Title: $<Mold Title>
            **  Build: A$<Boot-Version/3>
            **  File: $<Mold Stem>
            **  Author: $<Mold By>
            **  License: {
            **      Licensed under the Apache License, Version 2.0.
            **      See: http://www.apache.org/licenses/LICENSE-2.0
            **  }
        }
        if temporary [
            e/emit {
                **  Note: {AUTO-GENERATED FILE - Do not modify.}
            }
        ]
        e/emit {
            **
            ***********************************************************************/
        }
        e/emit newline
    ][
        e/emit mold/only compose/deep [
            REBOL [
                System: "REBOL [R3] Language Interpreter and Run-time Environment"
                Title: (title)
                File: (stem)
                Rights: {
                    Copyright 2012 REBOL Technologies
                    Copyright 2012-2018 Rebol Open Source Contributors
                    REBOL is a trademark of REBOL Technologies
                }
                License: {
                    Licensed under the Apache License, Version 2.0.
                    See: http://www.apache.org/licenses/LICENSE-2.0
                }
                (either temporary [
                    compose [Note: {AUTO-GENERATED FILE - Do not modify.}]
                ][
                    []
                ])
            ]
        ]
        e/emit newline
    ]
    return e
]
