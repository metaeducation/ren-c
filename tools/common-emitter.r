Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Common Code for Emitting Text Files"
    rights: {
        Copyright 2016-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    purpose: {
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
        [block! text! file!]
][
    if match [text! file!] template [
        template: reduce [template]
    ]
    else [
        assert [block? template]
        template: copy template
    ]

    return-type: type of last template

    all [
        text! <> return-type
        file! <> return-type
    ] then [
        panic ["CSCAPE requires TEXT! or FILE! as template:" mold last template]
    ]

    string: trim/auto to text! last template
    take back tail template

    ; As we process the string, we CHANGE any substitution expressions into
    ; an INTEGER! for doing the replacements later with REWORD (and not
    ; being ambiguous).
    ;
    num: 1
    num-text: to text! num ;-- CHANGE won't take GROUP! to evaluate, #1279

    list: collect [
        parse2 string [
          (col: 0)
          start:  ; <here>
          opt some [
            [
                (prefix: null suffix: null) finish:  ; <here>

                "${" change [copy expr: [to "}"]] num-text skip (
                    mode: #cname
                    pattern: unspaced ["${" num "}"]
                )
                    |
                "$<" change [copy expr: [to ">"]] num-text skip (
                    mode: #unspaced
                    pattern: unspaced ["$<" num ">"]
                )
                    |
                (prefix: copy/part start finish)
                "$[" change [copy expr: [to "]"]] num-text skip (
                    mode: #delimit
                    pattern: unspaced ["$[" num "]"]
                )
                copy suffix: to newline
                    |
                (prefix: copy/part start finish)
                "$(" change [copy expr: [to ")"]] num-text skip (
                    mode: #delimit
                    pattern: unspaced ["$(" num ")"]
                )
                copy suffix: remove to newline
            ] (
                keep/only compose [
                    (reify pattern) (col) (mode) (expr)
                        (reify prefix) (reify suffix)
                ]
                num: num + 1
                num-text: to text! num
            )
                |
            newline (col: 0 prefix: null suffix: null) start:  ; <here>
                |
            skip (col: col + 1)
        ]]
    ]

    if empty? list [return as return-type string]

    list: unique/case list

    substitutions: collect [
        for-each item list [
            pattern: degrade item/1
            col: item/2
            mode: item/3
            expr: item/4
            prefix: degrade item/5
            suffix: degrade item/6

            any-upper: did find/case expr charset [#"A" - #"Z"]
            any-lower: did find/case expr charset [#"a" - #"z"]
            keep pattern

            code: load/all expr
            for-each item template [  ; string was removed
                if get-word? item [
                    bind code get item
                ] else [
                    assert [word? item]
                    bind code binding of item
                ]
            ]
            sub: eval code

            sub: case [
                null? :sub [
                    panic [
                        "Substitution was null (old BLANK! in bootstrap):"
                            mold expr
                    ]
                ]
                any [
                    void? :sub  ; old NULL in bootstrap
                    sub = '~  ; weird new "accept reified" idea
                ][
                    copy "/* _ */"  ; replaced in post phase, vaporization
                ]
                mode = #cname [
                    reify if not all [text? sub  empty? sub] [
                        to-c-name sub
                    ]
                ]
                mode = #unspaced [
                    reify either block? sub [unspaced sub] [form sub]
                ]
                mode = #delimit [
                    reify delimit (unspaced [suffix newline]) sub
                ]
                panic ["Invalid CSCAPE mode:" mode]
            ]

            sub: any [degrade sub copy ""]

            case [
                all [any-upper  not any-lower] [uppercase sub]
                all [any-lower  not any-upper] [lowercase sub]
            ]

            ; If the substitution started at a certain column, make any line
            ; breaks continue at the same column.
            ;
            indent: unspaced collect [keep newline  keep opt prefix]
            replace sub newline indent

            keep sub
        ]
    ]

    for-each [pattern replacement] substitutions [
        replace string pattern replacement
    ]

    ; BLANK! in CSCAPE tries to be "smart" about omitting the item from its
    ; surrounding context, including removing lines when blank output and
    ; whitespace is all that ends up on them.  If the user doesn't want the
    ; intelligence, they should use "".
    ;
    parse2 string [
        (nonwhite: removed: null) start-line:
        while [
            space
            |
            newline
            [
                if (did all [not nonwhite  removed])
                :start-line remove thru [newline | end]
                |
                skip
            ]
            (nonwhite: removed: null) start-line:
            |
            remove "/* _ */" (removed: okay) opt remove space
            |
            (nonwhite: okay)
            skip
        ]
    ]

    return as return-type string
]


boot-version: load <../src/boot/version.r>

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
        panic [
            "File: should be set in the generating scripts header section"
            "so that generated files have a comment on what made them"
        ]
    ]

    print unspaced [{Generating "} title {" (via } by {)}]

    split-path/file file the stem:

    temporary: did any [
        temporary
        parse2/match stem ["tmp-" to end]
    ]

    is-c: did parse2/match stem [thru [".c" | ".h" | ".inc"]]

    is-js: did parse2/match stem [thru ".js" end]

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

            return: [~]
            data [block! text! char!]
            <with> buf-emit
        ][
            switch type of data [
                block! [
                    append buf-emit cscape data
                    append buf-emit newline
                ]
                text! [
                    append buf-emit cscape data
                ]
                char! [
                    append buf-emit data
                ]
            ]
        ]

        write-emitted: function [
            return: [~]
            /tabbed
            <with> file buf-emit
        ][
            if newline != last buf-emit [
                probe skip (tail-of buf-emit) -100
                panic "WRITE-EMITTED needs NEWLINE as last character in buffer"
            ]

            if tab-pos: find buf-emit tab [
                probe skip tab-pos -100
                panic "tab character passed to emit"
            ]

            if tabbed [
                replace buf-emit spaced-tab tab
            ]

            print [{WRITING =>} file]

            write-if-changed file buf-emit

            ; For clarity/simplicity, emitters are not reused.
            ;
            file: null
            buf-emit: null
        ]
    ]

    if any [is-c is-js] [
        e/emit [title stem by temporary {
            /**********************************************************************
            **
            **  Rebol [R3] Language Interpreter and Run-time Environment
            **  Copyright 2012 REBOL Technologies
            **  Copyright 2012-2018 Rebol Open Source Contributors
            **  REBOL is a trademark of REBOL Technologies
            **  Licensed under the Apache License, Version 2.0
            **
            ************************************************************************
            **
            **  title: $<Mold Title>
            **  build: A$<Boot-Version/3>
            **  file: $<Mold Stem>
            **  author: $<Mold By>
            **  license: {
            **      Licensed under the Apache License, Version 2.0.
            **      See: http://www.apache.org/licenses/LICENSE-2.0
            **  }
            **  notes: "AUTO-GENERATED FILE - Do not modify."
            **
            ***********************************************************************/
        }]
    ]
    else [
        e/emit [title stem temporary {
            Rebol [
                system: "Rebol [R3] Language Interpreter and Run-time Environment"
                title: (title)
                file: (stem)
                rights: {
                    Copyright 2012 REBOL Technologies
                    Copyright 2012-2018 Rebol Open Source Contributors
                    REBOL is a trademark of REBOL Technologies
                }
                license: {
                    Licensed under the Apache License, Version 2.0.
                    See: http://www.apache.org/licenses/LICENSE-2.0
                }
                notes: "AUTO-GENERATED FILE - Do not modify."
            ]
        }]
    ]
    return e
]
