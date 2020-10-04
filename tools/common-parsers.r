REBOL [
    System: "Ren-C Core Extraction of the Rebol System"
    Title: "Common Parsers for Tools"
    Rights: {
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren-C is Copyright 2015-2018 MetaEducation
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "@codebybrett"
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        These are some common routines used by the utilities
        that build and test the system.
    }
]

do %c-lexicals.r
do %text-lines.reb
do %parsing-tools.reb

decode-key-value-text: function [
    {Decode key value formatted text.}
    text [text!]
][
    
    data-fields: [
        any [
            position:
            data-field
            | newline
        ]
        end
    ]

    data-field: [
        data-field-name eof: [
            #" " to newline any [
                newline not data-field-name not newline to newline
            ]
            | any [1 2 newline 2 20 #" " to newline]
        ] eol: (emit-meta) newline
    ]

    data-field-char: charset [#"A" - #"Z" #"a" - #"z"]
    data-field-name: [some data-field-char any [#" " some data-field-char] #":"]

    emit-meta: func [<local> key] [
        key: replace/all copy/part position eof #" " #"-"
        remove back tail-of key
        append meta reduce [
            to word! key
            trim/auto copy/part eof eol
        ]
    ]

    meta: copy []

    parse text data-fields else [
        fail [
            {Expected key value format on line} (text-line-of position)
            {and lines must end with newline.}
        ]
    ]

    new-line/all/skip meta true 2
]

load-until-blank: function [
    {Load rebol values from text until double newline.}
    text [text!]
    /next {Return values and next position.}
] [
    wsp: compose [some (charset { ^-})]

    res: _  ; !!! collect as SET-WORD!s for locals, evolving...
    rebol-value: parsing-at x [
        ;
        ; !!! SET-BLOCK! not bootstrap
        ;
        attempt [transcode/next x 'res] else [res: blank]
        res
    ]

    terminator: [opt wsp newline opt wsp newline]

    rule: [
        some [not terminator rebol-value]
        opt wsp opt [1 2 newline] position: to end
    ]

    either parse text rule [
        values: load copy/part text position
        reduce [values position]
    ][
        blank
    ]
]


collapse-whitespace: [some [change some white-space space | skip] end]
bind collapse-whitespace c-lexical/grammar


proto-parser: context [

    emit-fileheader: _
    emit-proto: _
    emit-directive: _
    parse-position: _
    notes: _
    lines: _
    proto-id: _
    proto-arg-1: _
    data: _
    eoh: _ ; End of file header.

    process: func [return: <void> text] [
        parse text [grammar/rule]  ; Review: no END (return result unused?)
    ]

    grammar: context bind [

        rule: [
            parse-position: opt fileheader
            any [parse-position: segment]
        ]

        fileheader: [
            (data: _)
            doubleslashed-lines
            and is-fileheader
            eoh:
            (
                emit-fileheader data
            )
        ]

        segment: [
            (proto-id: proto-arg-1: _)
            format-func-section
            | span-comment
            | line-comment any [newline line-comment] newline
            | opt wsp directive
            | other-segment
        ]

        directive: [
            copy data [
                ["#ifndef" | "#ifdef" | "#if" | "#else" | "#elif" | "#endif"]
                any [not newline c-pp-token]
            ] eol
            (
                emit-directive data
            )
        ]

        ; We COPY/DEEP here because this part gets invasively modified by
        ; the source analysis tools.
        ;
        other-segment: copy/deep [thru newline]

        ; we COPY/DEEP here because this part gets invasively modified by
        ; the source analysis tools.
        ;
        format-func-section: copy/deep [
            doubleslashed-lines
            and is-intro
            function-proto any white-space
            function-body
            (
                ; EMIT-PROTO doesn't want to see extra whitespace (such as
                ; when individual parameters are on their own lines).
                ;
                parse proto collapse-whitespace
                proto: trim proto
                assert [find proto "("]

                if find proto "()" [
                    print [
                        proto
                        newline
                        {C-Style no args should be foo(void) and not foo()}
                        newline
                        http://stackoverflow.com/q/693788/c-void-arguments
                    ]
                    fail "C++ no-arg prototype used instead of C style"
                ]

                ; Call the EMIT-PROTO hook that the client provided.  They
                ; receive the stripped prototype as a formal parameter, but
                ; can also examine state variables of the parser to extract
                ; other properties--such as the processed intro block.
                ;
                emit-proto proto 
            )
        ]

        function-body: #"{"

        doubleslashed-lines: [copy lines some ["//" thru newline]]

        is-fileheader: parsing-at position [
            try all [
                lines: attempt [decode-lines lines {//} { }]
                parse lines [copy data to {=///} to end]
                data: attempt [load-until-blank trim/auto data]
                data: attempt [
                    if set-word? first data/1 [data/1]
                ]
                position ; Success.
            ]
        ]

        is-intro: parsing-at position [
            try all [
                lines: attempt [decode-lines lines {//} { }]
                data: load-until-blank lines
                data: attempt [
                    ;
                    ; !!! The recognition of Rebol-styled comment headers
                    ; originally looked for SET-WORD!, but the syntax for
                    ; doing export uses a WORD! (EXPORT) before the SET-WORD!
                    ;
                    ; http://www.rebol.net/r3blogs/0300.html
                    ;
                    ; It's hacky to just throw it in here, but the general
                    ; consensus is that the build process needs to be made
                    ; much simpler.  It really should be going by seeing it
                    ; is a REBNATIVE() vs. worrying too much about the text
                    ; pattern in the comment being detected.
                    ;
                    if any [
                        set-word? first data/1
                        'export = first data/1
                    ][
                        notes: data/2
                        data/1
                    ]
                ]
                position ; Success.
            ]
        ]

        ; http://blog.hostilefork.com/kinda-smart-pointers-in-c/
        ;
        ;     TYPEMACRO(*) Some_Function(TYPEMACRO(const*) value, ...)
        ;     { ...
        ;
        typemacro-parentheses: [
            "(*)" | "(const*)"
            | "(const *)" (fail "use (const*) not (const *)")
        ]

        function-proto: [
            copy proto [
                not white-space
                some [
                    typemacro-parentheses
                    | [
                        not "(" not "="
                        [white-space | copy proto-id identifier | skip]
                    ]
                ]
                "("
                any white-space
                opt [
                    not typemacro-parentheses
                    not ")"
                    copy proto-arg-1 identifier
                ]
                any [typemacro-parentheses | not ")" [white-space | skip]]
                ")"
            ]
        ]

    ] c-lexical/grammar
]

rewrite-if-directives: function [
    {Bottom up rewrite conditional directives to remove unnecessary sections.}
    position
][
    until [
        parse position [
            (rewritten: false)
            some [
                [
                    change ["#if" thru newline "#endif" thru newline] ""
                    | change ["#elif" thru newline "#endif"] "#endif"
                    | change ["#else" thru newline "#endif"] "#endif"
                ] (rewritten: true) :position
                | thru newline
            ]
            end
        ]
        not rewritten
    ]
]
