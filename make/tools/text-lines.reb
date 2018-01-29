REBOL [
    Title: "Text Lines"
    Version: 1.0.0
    Rights: {
        Copyright 2015 Brett Handley

        Rebol3 load-next by Chris Ross-Gill.
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Brett Handley"
    Purpose: {Functions operating on lines of text.}
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
            {Expected line} (text-line-of text pos)
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

for-each-line: function [
    {Iterate over text lines.}

    'record [word!]
        {Word set to metadata for each line.}
    text [string!]
        {Text with lines.}
    body [block!]
        {Block to evaluate each time.}
][
    while [not tail? text] [
        eol: any [
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

lines-exceeding: function [
    {Return the line numbers of lines exceeding line-length.}
    line-length [integer!]
    text [string!]
] [

    line-list: line: _

    count-line: [
        (
            line: 1 + any [line 0]
            if line-length < subtract index-of eol index of bol [
                append line-list: any [line-list copy []] line
            ]
        )
    ]

    parse text [
        any [bol: to newline eol: skip count-line]
        bol: skip to end eol: count-line
    ]

    line-list
]

text-line-of: function [
    {Returns line number of position within text.}
    position [string!]
] [

    ; Here newline is considered last character of a line.
    ; No counting performed for empty text.
    ; Line 0 does not exist.

    text: head-of position
    idx: index-of position
    line: 0

    advance: [skip (line: line + 1)]

    parse text [
        any [
            to newline cursor:
            if (lesser? index-of cursor idx)
            advance
        ]
        advance
    ]

    if zero? line [line: _]
   
    line
]
