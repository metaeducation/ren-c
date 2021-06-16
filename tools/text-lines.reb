REBOL [
    Title: "Text Lines"
    Version: 1.0.0
    Rights: {
        Copyright 2015 Brett Handley
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Brett Handley"
    Purpose: {Functions operating on lines of text.}
]

decode-lines: func [
    {Decode text encoded using a line prefix e.g. comments (modifies).}
    text [text!]
    line-prefix [text! block!] {Usually "**" or "//". Matched using parse.}
    indent [text! block!] {Usually "  ". Matched using parse.}
] [
    let pattern: compose/only [(line-prefix)]
    if not empty? indent [append pattern compose/only [opt (indent)]]

    let [pos rest]
    let line-rule: [
        pos: here pattern rest: here
        (rest: remove/part pos rest)
        seek :rest  ; GET-WORD! for bootstrap (SEEK is no-op)
        thru newline
    ]
    parse text [while line-rule end] else [
        fail [
            {Expected line} (try text-line-of text pos)
            {to begin with} (mold line-prefix)
            {and end with newline.}
        ]
    ]
    if pos: back tail-of text [remove pos]
    text
]

encode-lines: func [
    {Encode text using a line prefix (e.g. comments)}

    text [text!]
    line-prefix [text!] {Usually "**" or "//"}
    indent [text!] {Usually "  "}
] [
    ; Note: Preserves newline formatting of the block.

    ; Encode newlines.
    let bol: join line-prefix indent
    let pos
    parse text [
        while [
            thru newline pos: here
            [
                newline (pos: insert pos line-prefix)
              | (pos: insert pos bol)
            ] seek :pos  ; GET-WORD! for bootstrap (SEEK is no-op)
        ]
        end
    ]

    ; Indent head if original text did not start with a newline.
    pos: insert text line-prefix
    if not equal? newline :pos/1 [insert pos indent]

    ; Clear indent from tail if present.
    if indent = pos: skip tail-of text 0 - length of indent [clear pos]
    append text newline

    text
]

for-each-line: func [
    {Iterate over text lines}

    'record "Word set to metadata for each line"
        [word!]
    text "Text with lines"
        [text!]
    body "Block to evaluate each time"
        [block!]
] [
    loop [not tail? text] [
        let eol: any [
            find text newline
            tail of text
        ]

        set record compose [
            position (text) length (subtract index of eol index of text)
        ]
        text: next eol

        do body
    ]
]

lines-exceeding: func [  ; !!! Doesn't appear used, except in tests (?)
    {Return the line numbers of lines exceeding line-length.}

    return: "Returns null if no lines (is this better than returning []?)"
        [<opt> block!]
    line-length [integer!]
    text [text!]
] [
    let line-list: _
    let line: _

    count-line-rule: [
        (
            line: 1 + any [line 0]
            if line-length < subtract index-of eol index of bol [
                append line-list: any [line-list copy []] line
            ]
        )
    ]

    let [eol bol]
    parse text [
        while [bol: here to newline eol: here skip count-line-rule]
        bol: here skip to end eol: here count-line
        end
    ]

    opt line-list
]

text-line-of: func [
    {Returns line number of position within text}

    return: "Line 0 does not exist, no counting is performed for empty text"
        [<opt> integer!]
    position "Position (newline is considered the last character of a line)"
        [text! binary!]
] [
    let text: head of position
    let idx: index of position
    let line: 0

    let advance-rule: [skip (line: line + 1)]

    parse text [
        while [
            to newline cursor: here

            ; IF deprecated in Ren-C, but :(...) with logic not available
            ; in the bootstrap build.
            ;
            if (lesser? index of cursor idx)

            advance-rule
        ]
        advance-rule
    ]

    if zero? line [return null]
    line
]

text-location-of: func [
    {Returns line and column of position within text.}
    position [text! binary!]
] [
    ; Here newline is considered last character of a line.
    ; No counting performed for empty text.
    ; Line 0 does not exist.

    let text: head of position
    let idx: index of position
    let line: 0

    advance-rule: [eol: here skip (line: line + 1)]

    parse text [
        while [
            to newline cursor: here

            ; !!! IF is deprecated in PARSE, but this code is expected to work
            ; in bootstrap.
            ;
            if (lesser? index of cursor idx)

            advance-rule
        ]
        advance-rule
        end
    ]

    if zero? line [line: _] else [
        line: reduce [line 1 + subtract index? position index? eol]
    ]

    line
]
