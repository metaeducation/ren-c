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

    emit-meta: func [/local key] [
        key: replace/all copy/part position eof #" " #"-"
        remove back tail key
        append meta reduce [
            to word! key
            trim/auto copy/part eof eol
        ]
    ]

    meta: make block! []
    
    if not parse/all text data-fields [
        fail [{Expected key value format on line} (line-of text position) {and lines must end with newline.}]
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
    if not parse/all text [any line] [
        fail [{Expected line} (line-of text pos) {to begin with} (mold line-prefix) {and end with newline.}]
    ]
    remove back tail text
    text
]


encode-lines: func [
    {Encode text using a line prefix (e.g. comments).}
    text [string!]
    line-prefix [string!] {Usually "**" or "//".}
    indent [string!] {Usually "  ".}
    /local bol pos
] [

    ; Note: Preserves newline formatting of the block.

    ; Encode newlines.
    replace/all text newline rejoin [newline line-prefix indent]

    ; Indent head if original text did not start with a newline.
    pos: insert text line-prefix
    if not equal? newline pos/1 [insert pos indent]

    ; Clear indent from tail if present.
    if indent = pos: skip tail text 0 - length indent [clear pos]
    append text newline

    text
]


line-of: function [
    {Returns line number of position within text.}
    text [string!]
    position [string! integer!]
] [

    if integer? position [
        position: at text position
    ]

    line: none

    count-line: [(line: 1 + any [line 0])]

    parse/all copy/part text next position [
        any [to newline skip count-line] skip count-line
    ]

    line
]


load-next: function [
    {Load the next value. Return block with value and new position.}
    string [string!]
] [
    out: transcode/next to binary! string
    out/2: skip string subtract length string length to string! out/2
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
        either empty? res [none] [second res]
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
        none
    ]
]


parsing-at: func [
    {Defines a rule which evaluates a block for the next input position, fails otherwise.}
    'word [word!] {Word set to input position (will be local).}
    block [block!] {Block to evaluate. Return next input position, or none/false.}
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


proto-parser: context [

    emit-proto: none
    proto-prefix: none
    parse.position: none
    notes: none
    lines: none
    proto.id: none
    proto.arg.1: none
    data: none
    style: none

    process: func [data] [parse data grammar/rule]

    grammar: context bind [

        rule: [
            any [parse.position: segment]
        ]

        segment: [
            (style: proto.id: proto.arg.1: none)
            format2015-func-section
            | other-segment
        ]

        other-segment: [thru newline]

        format2015-func-section: [
            doubleslashed-lines
            and is-format2015-intro
            function-proto some white-space
            function-body
            (
                style: 'format2015
                emit-proto proto
            )
        ]

        function-body: #"{"

        doubleslashed-lines: [copy lines some ["//" thru newline]]

        is-format2015-intro: parsing-at position [
            either all [
                lines: attempt [decode-lines lines {//} { }]
                data: load-until-blank lines
                data: attempt [
                    either set-word? first data/1 [
                        notes: data/2
                        data/1
                    ][
                        none
                    ]
                ]
            ] [
                position ; Success.
            ][
                none
            ]
        ]

        function-proto: [
            proto-prefix copy proto [
                not white-space
                some [not #"(" not #"=" [white-space | copy proto.id identifier | skip]] #"("
                any white-space
                opt [not #")" copy proto.arg.1 identifier]
                any [not #")" [white-space | skip]] #")"
            ]
        ]

    ] c.lexical/grammar
]
