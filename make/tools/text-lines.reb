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
    {Decode text encoded using a line prefix e.g. comments (modifies).}
    text [text!]
    line-prefix [text! block!] {Usually "**" or "//". Matched using parse.}
    indent [text! block!] {Usually "  ". Matched using parse.}
] [
    pattern: compose/only [(line-prefix)]
    if not empty? indent [append pattern compose/only [opt (indent)]]
    line: [pos: pattern rest: (rest: remove/part pos rest) :rest thru newline]
    if not parse text [any line] [
        fail [
            {Expected line} (try text-line-of text pos)
            {to begin with} (mold line-prefix)
            {and end with newline.}
        ]
    ]
    if pos: try back tail-of text [remove pos]
    text
]

encode-lines: func [
    {Encode text using a line prefix (e.g. comments).}
    text [text!]
    line-prefix [text!] {Usually "**" or "//".}
    indent [text!] {Usually "  ".}
    <local> bol pos
][
    ; Note: Preserves newline formatting of the block.

    ; Encode newlines.
    bol: join-of line-prefix indent
    parse text [
        any [
            thru newline pos:
            [newline (pos: insert pos line-prefix) | (pos: insert pos bol)] :pos
        ]
    ]

    ; Indent head if original text did not start with a newline.
    pos: insert text line-prefix
    if newline !== :pos/1 [insert pos indent]

    ; Clear indent from tail if present.
    if indent == pos: skip tail-of text 0 - length of indent [clear pos]
    append text newline

    text
]

for-each-line: function [
    {Iterate over text lines.}

    'record [word!]
        {Word set to metadata for each line.}
    text [text!]
        {Text with lines.}
    body [block!]
        {Block to evaluate each time.}
][
    while-not [tail? text] [
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

lines-exceeding: function [ ;-- !!! Doesn't appear used, except in tests (?)
    {Return the line numbers of lines exceeding line-length.}

    return: [<opt> block!]
        "Returns null if no lines (is this better than returning []?)"
    line-length [integer!]
    text [text!]
][
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

    opt line-list
]

text-line-of: function [
    {Returns line number of position within text}

    return: [<opt> integer!]
        "Line 0 does not exist, and no counting is performed for empty text"
    position [text! binary!]
        "Position, where newline is considered the last character of a line"
][
    text: head of position
    idx: index of position
    line: 0

    advance: [skip (line: line + 1)]

    parse text [
        any [
            to newline cursor:
            if (lesser? index of cursor idx)
            advance
        ]
        advance
    ]

    if zero? line [return null]
    line
]

text-location-of: function [
    {Returns line and column of position within text.}
    position [text! binary!]
] [

    ; Here newline is considered last character of a line.
    ; No counting performed for empty text.
    ; Line 0 does not exist.

    text: head of position
    idx: index of position
    line: 0

    advance: [eol: skip (line: line + 1)]

    parse text [
        any [
            to newline cursor:
            if (lesser? index of cursor idx)
            advance
        ]
        advance
    ]

    if zero? line [line: _] else [
        line: reduce [line 1 + subtract index? position index? eol]
    ]
   
    line
]
