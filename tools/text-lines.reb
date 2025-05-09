Rebol [
    title: "Text Lines"
    version: 1.0.0
    type: module
    name: Text-Lines
    rights: --[
        Copyright 2015 Brett Handley
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    author: "Brett Handley"
    purpose: "Functions operating on lines of text"
]

import <bootstrap-shim.r>

decode-lines: func [
    "Decode text encoded using a line prefix e.g. comments (modifies)"
    text [~null~ text! error!]  ; raised? in modern exe, not ERROR!
    line-prefix [text! block!] "matched using parse, usually ** or // -"
    indent [text! block!] -[Matched using parse, usually "  "]-
] [
    let pattern: compose [(line-prefix)]
    if not empty? indent [append pattern compose [opt (indent)]]

    let [pos rest]
    let line-rule: [
        pos: <here>
        pattern
        rest: <here>
        (rest: remove:part pos rest)
        seek rest
        thru newline
    ]
    parse3:match text [opt some line-rule] else [
        return fail [
            "Expected line" (reify text-line-of text pos)
            "to begin with" (mold line-prefix)
            "and end with newline."
        ]
    ]
    if pos: back tail of text [remove pos]
    return text
]

encode-lines: func [
    "Encode text using a line prefix (e.g. comments)"

    text [text!]
    line-prefix [text!] "Usually ** or //"
    indent [text!] "Usually two spaces"
] [
    ; Note: Preserves newline formatting of the block.

    ; Encode newlines.
    let bol: join line-prefix indent
    let pos
    parse3 text [
        opt some [
            thru newline, pos: <here>
            [
                newline (pos: insert pos line-prefix)
              | (pos: insert pos bol)
            ]
            seek pos
        ]
        to <end>
    ]

    ; Indent head if original text did not start with a newline.
    pos: insert text line-prefix
    if not equal? newline try pos.1 [insert pos indent]

    ; Clear indent from tail if present.
    if indent = pos: skip tail of text 0 - length of indent [clear pos]
    append text newline

    return text
]

for-each-line: func [
    "Iterate over text lines"

    return: [~]
    var "Word set to metadata for each line"
        [word!]
    text "Text with lines"
        [text!]
    body "Block to evaluate each time"
        [block!]
    <local> obj
][
    obj: construct compose [(setify var) ~]  ; make variable
    body: overbind obj body  ; make variable visible to body
    var: has obj var

    while [not tail? text] [
        let eol: any [find text newline, tail of text]

        set var compose [
            position (text) length (subtract index of eol index of text)
        ]
        text: next eol

        eval body
    ]
]

lines-exceeding: func [  ; !!! Doesn't appear used, except in tests (?)
    "Return the line numbers of lines exceeding line-length"

    return: "Returns null if no lines (is this better than returning []?)"
        [~null~ block!]
    line-length [integer!]
    text [text!]
] [
    let line-list: null
    let line: null
    let [eol bol]

    let count-line-rule: [
        (
            line: 1 + any [line, 0]
            if line-length < subtract index-of eol index of bol [
                append line-list: any [line-list, copy []] line
            ]
        )
    ]

    parse3:match text [
        opt some [
            bol: <here>
            to newline
            eol: <here>
            one
            count-line-rule
        ]
        bol: <here>
        one, to <end>, eol: <here>
        count-line-rule
    ] else [
        return null
    ]

    return line-list
]

text-line-of: func [
    "Returns line number of position within text"

    return: "Line 0 does not exist, no counting is performed for empty text"
        [~null~ integer!]
    position "Position (newline is considered the last character of a line)"
        [text! blob!]
] [
    let text: head of position
    let idx: index of position
    let line: 0

    let advance-rule: [one (line: line + 1)]

    let cursor
    parse3:match text [
        opt some [
            to newline cursor: <here>

            when (lesser? index of cursor idx)

            advance-rule
        ]
        advance-rule
        to <end>
    ] else [
        return null
    ]

    if zero? line [return null]
    return line
]

text-location-of: func [
    "Returns line and column of position within text"
    position [text! blob!]
] [
    ; Here newline is considered last character of a line.
    ; No counting performed for empty text.
    ; Line 0 does not exist.

    let text: head of position
    let idx: index of position
    let line: 0
    let eol

    let advance-rule: [
        eol: <here>
        one (line: line + 1)
    ]
    let cursor
    parse3 text [
        opt some [
            to newline cursor: <here>

            ; !!! IF is deprecated in PARSE, but this code is expected to work
            ; in bootstrap.
            ;
            when (lesser? index of cursor idx)

            advance-rule
        ]
        advance-rule
        to <end>
    ]

    if zero? line [line: null] else [
        line: reduce [line 1 + subtract (index of position) (index of eol)]
    ]

    return line
]

export [
    decode-lines
    encode-lines
    for-each-line
    lines-exceeding
    text-line-of
    text-location-of
]
