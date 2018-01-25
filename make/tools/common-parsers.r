REBOL [
    System: "Ren/C Core Extraction of the Rebol System"
    Title: "Common Parsers for Tools"
    Rights: {
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren/C is Copyright 2015 MetaEducation
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
        that build the system, which are found in %src/tools/
    }
]

do %c-lexicals.r

decode-key-value-text: function [
    {Decode key value formatted text.}
    text [string!]
][
    
    data-fields: [
        any [
            position:
            data-field
            | newline
        ]
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

    meta: make block! []
    
    if not parse text data-fields [
        fail [
            {Expected key value format on line} (line-of text position)
            {and lines must end with newline.}
        ]
    ]

    new-line/all/skip meta true 2
]


decode-lines: function [
    {Decode text previously encoded using a line prefix e.g. comments (modifies).}
    text [string!]
    line-prefix [string! block!] {Usually "**" or "//". Matched using parse.}
    indent [string! block!] {Usually "  ". Matched using parse.}
] [
    pattern: compose/only [(line-prefix)]
    if not empty? indent [append pattern compose/only [opt (indent)]]
    line: [pos: pattern rest: (rest: remove/part pos rest) :rest thru newline]
    if not parse text [any line] [
        fail [
            {Expected line} (line-of text pos)
            {to begin with} (mold line-prefix)
            {and end with newline.}
        ]
    ]
    remove back tail-of text
    text
]


encode-lines: func [
    {Encode text using a line prefix (e.g. comments).}
    text [string!]
    line-prefix [string!] {Usually "**" or "//".}
    indent [string!] {Usually "  ".}
    <local> bol pos
][
    ; Note: Preserves newline formatting of the block.

    ; Encode newlines.
    replace/all text newline unspaced [newline line-prefix indent]

    ; Indent head if original text did not start with a newline.
    pos: insert text line-prefix
    if not equal? newline :pos/1 [insert pos indent]

    ; Clear indent from tail if present.
    if indent = pos: skip tail-of text 0 - length-of indent [clear pos]
    append text newline

    text
]


line-from-pos: function [
    {Returns line number of position within text.}
    text [string!]
    position [string! integer!]
] [

    if integer? position [
        position: at text position
    ]

    line: _

    count-line: [(line: 1 + any [line 0])]

    parse copy/part text next position [
        any [to newline skip count-line] skip count-line
    ]

    line
]

load-next: function [
    {Load the next value. Return block with value and new position.}
    string [string!]
][
    out: transcode/next to binary! string
    out/2: skip string subtract length-of string length-of to string! out/2
    out
] ; by @rgchris.


load-until-blank: function [
    {Load rebol values from text until double newline.}
    text [string!]
    /next {Return values and next position.}
] [

    wsp: compose [some (charset { ^-})]

    rebol-value: parsing-at x [
        res: any [attempt [load-next x] []]
        either empty? res [blank] [second res]
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


parsing-at: func [
    {Defines a rule which evaluates a block for the next input position, fails otherwise.}
    'word [word!] {Word set to input position (will be local).}
    block [block!]
        {Block to evaluate. Return next input position, or blank/false.}
    /end {Drop the default tail check (allows evaluation at the tail).}
] [
    use [result position][
        block: compose/only [to-value (to group! block)]
        if not end [
            block: compose/deep [all [not tail? (word) (block)]]
        ]
        block: compose/deep [result: either position: (block) [[:position]] [[end skip]]]
        use compose [(word)] compose/deep [
            [(to set-word! :word) (to group! block) result]
        ]
    ]
]


collapse-whitespace: [some [change some white-space space | skip]]
bind collapse-whitespace c.lexical/grammar


proto-parser: context [

    emit-fileheader: _
    emit-proto: _
    emit-directive: _
    parse.position: _
    notes: _
    lines: _
    proto.id: _
    proto.arg.1: _
    data: _
    eoh: _ ; End of file header.

    process: func [text] [parse text grammar/rule]

    grammar: context bind [

        rule: [
            parse.position: opt fileheader
            any [parse.position: segment]
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
            (proto.id: proto.arg.1: _)
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
                ; !!! A feature allowing the RL_API to comment the arguments
                ; to an API with line comments on the individual C parameters
                ; might be interesting.
                ;
                parse proto collapse-whitespace
                proto: trim proto

                ; !!! Some EMIT-PROTO hooks were checking this themselves and
                ; then doing nothing if they couldn't find a left paren.  Not
                ; clear why they were doing that, so assert just in case.
                ;
                assert [find proto "("]

                ; Our parsing is all C-based, so EMIT-PROTO clients should not
                ; be accepting C++-style prototypes for no-argument functions.
                ;
                ; !!! Theoretically a prototype could be a zero-argument macro
                ; that expands into something which did have arguments.  Could
                ; there be a purpose for that?
                ;
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
            either all [
                lines: attempt [decode-lines lines {//} { }]
                parse lines [copy data to {=///} to end]
                data: attempt [load-until-blank trim/auto data]
                data: attempt [
                    either set-word? first data/1 [data/1][blank]
                ]
            ][
                position ; Success.
            ][
                blank
            ]
        ]

        is-intro: parsing-at position [
            either all [
                lines: attempt [decode-lines lines {//} { }]
                data: load-until-blank lines
                data: attempt [
                    either set-word? first data/1 [
                        notes: data/2
                        data/1
                    ][
                        blank
                    ]
                ]
            ][
                position ; Success.
            ][
                blank
            ]
        ]


        ; With types being able to be parameterized macros, then function
        ; prototypes can look like:
        ;
        ;     TYPEMACRO(*) Some_Function(TYPEMACRO(const *) value, ...)
        ;     { ...
        ;
        ; !!! Matching the parentheses strings that exist in the code
        ; explicitly is a maybe-temporary hack.  Though as the pattern being
        ; looked for is a preprocessor trick, it's outside the C spec so
        ; anything will be "hacky".
        ;
        typemacro-parentheses: [
            "(*)" | "(const *)"
        ]

        function-proto: [
            copy proto [
                not white-space
                some [
                    typemacro-parentheses
                    | [
                        not "(" not "="
                        [white-space | copy proto.id identifier | skip]
                    ]
                ]
                "("
                any white-space
                opt [
                    not typemacro-parentheses
                    not ")"
                    copy proto.arg.1 identifier
                ]
                any [typemacro-parentheses | not ")" [white-space | skip]]
                ")"
            ]
        ]

    ] c.lexical/grammar
]

rewrite-if-directives: function [
    {Bottom up rewrite conditional directives to remove unnecessary sections.}
    position
][
    loop-until [
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
        ]
        not rewritten
    ]
]
