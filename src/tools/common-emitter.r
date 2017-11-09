REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Common Code for Emitting Text Files"
    Rights: {
        Copyright 2016-2017 Rebol Open Source Contributors
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

boot-version: load %../boot/version.r

make-emitter: function [
    {Create a buffered output text file emitter}

    title [string!]
        {Title to be placed in the comment header (header matches file type)}
    file [file!]
        {Filename to be emitted... .r/.reb/.c/.h/.inc files supported}
    /temporary
        {Add a DO-NOT-EDIT warning (automatic if file begins with 'tmp-')}
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

    temporary: any [temporary | parse stem ["tmp-" to end]]

    is-c: parse stem [[thru ".c" | thru ".h" | thru ".inc"] end]

    e: make object! compose [
        ;
        ; NOTE: %make-headers.r directly manipulates the buffer, because it
        ; wishes to merge #ifdef/#endif cases
        ;
        ; !!! Should the allocation size be configurable?
        ;
        buf-emit: make string! 32000

        file: (file)
        title: (title)

        emit: proc [data] [
            adjoin buf-emit data
        ]


        unemit: proc [
            data [char!]
        ][
            if data != last buf-emit [
                probe skip (tail buf-emit) -100
                fail ["UNEMIT did not match" data "as the last piece of input"]
            ]
            assert [data = last buf-emit]
            take/last buf-emit
        ]


        emit-line: proc [data /indent] [
            unless any [tail? buf-emit | newline = last buf-emit] [
                probe skip (tail buf-emit) -100
                fail "EMIT-LINE should always start a new line"
            ]
            data: reduce data
            if find data newline [
                probe data
                fail "Data passed to EMIT-LINE had embedded newlines"
            ]
            if parse data [thru space end] [
                probe data
                fail "Data passed to EMIT-LINE had trailing whitespace"
            ]
            if indent [emit spaced-tab]
            emit data
            emit newline
        ]


        emit-lines: procedure [block [block!] /indent] [
            for-each data block [
                either indent [
                    emit-line/indent data
                ][
                    emit-line data
                ]
            ]
        ]

        emit-item: procedure [
            {Emits identifier and comma for enums and initializer lists}
            name
                {Converted using TO-C-NAME which joins BLOCK! and forms WORD!}
            /upper
                {Make name uppercase (-after- the TO-C-NAME conversion)}
            /assign
                {Give the item an assigned value}
            num [integer!]
        ][
            name: to-c-name name
            if upper [uppercase name]
            either assign [
                emit-line/indent [name space "=" space num ","]
            ][
                emit-line/indent [name ","]
            ]

            ; NOTE: standard C++ and C do not like commas on the last item in
            ; lists, so they are removed with EMIT-END, by taking the last
            ; comma out of the emit buffer.
        ]


        emit-annotation: procedure [
            {Adds a C++ "//"-style comment to end of the last line emitted.}
            note [word! string! integer!]
        ][
            unemit newline
            emit [space "//" space note newline]
        ]


        emit-end: procedure [] [
            remove find/last buf-emit #","
            emit-line ["};"]
            emit newline
        ]


        write-emitted: procedure [
            /tabbed
        ][
            if newline != last buf-emit [
                probe skip (tail buf-emit) -100
                fail "WRITE-EMITTED needs NEWLINE as last character in buffer"
            ]

            if tab-pos: find buf-emit tab [
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

    either is-c [
        e/emit-lines [
            {/***********************************************************************}
            {**}
            {**  REBOL [R3] Language Interpreter and Run-time Environment}
            {**  Copyright 2012 REBOL Technologies}
            {**  REBOL is a trademark of REBOL Technologies}
            {**  Licensed under the Apache License, Version 2.0}
            {**}
            {************************************************************************}
            {**}
            [{**  Title: } title]
            [{**  Build: A} boot-version/3]
            [{**  File:  } stem]
            {**}
        ]
        if temporary [
            e/emit-lines [
                [{**  !!!}]
                [{**  !!! **WARNING!**  DO NOT EDIT THIS FILE DIRECTLY}]
                [{**  !!! IT IS GENERATED BY} by]
                [{**  !!!}]
            ]
        ]
        e/emit-line [
            {***********************************************************************/}
        ]
        e/emit newline
    ][
        e/emit mold/only compose/deep [
            REBOL [
                System: "REBOL [R3] Language Interpreter and Run-time Environment"
                Title: (title)
                File: (stem)
                Rights: {
                    Copyright 2012 REBOL Technologies
                    Copyright 2012-2017 Rebol Open Source Contributors
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
