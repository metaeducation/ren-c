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
    {Decode text previously encoded using a line prefix, e.g. comments (modifies).}
    text [string!]
    line-prefix [string!] {Usually "**" or "//".}
    indent [string!] {Usually "  ".}
] [
    if not parse text [any [line-prefix thru newline]] [
        fail [{decode-lines expects each line to begin with} (mold line-prefix) { and finish with a newline.}]
    ]
    insert text newline
    replace/all text join-of newline line-prefix newline
    if not empty? indent [
        replace/all text join-of newline indent newline
    ]
    remove text
    remove back tail text
    text
]

encode-lines: func [
    {Encode text using a line prefix, e.g. comments (modifies).}
    text [string!]
    line-prefix [string!] {Usually "**" or "//".}
    indent [string!] {Usually "  ".}
    /local bol pos
] [

    ; Note: Preserves newline formatting of the block.

    ; Encode newlines.
    replace/all text newline unspaced [newline line-prefix indent]

    ; Indent head if original text did not start with a newline.
    pos: insert text line-prefix
    if not equal? newline pos/1 [insert pos indent]

    ; Clear indent from tail if present.
    if indent = pos: skip tail text 0 - length-of indent [clear pos]
    append text newline

    text
]

for-each-line: func [
    {Iterate over text lines.}
    'record [word!] {Word set to metadata for each line.}
    text [string!] {Text with lines.}
    body [block!] {Block to evaluate each time.}
    /local eol
] [

    set/only 'result while [not tail? text] [

        eol: any [
            find text newline
            tail text
        ]

        set record compose [position (text) length (subtract index-of eol index-of text)]
        text: next eol

        do body
    ]

    get/only 'result
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
            if line-length < subtract index-of eol index-of bol [
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

line-of: function [
    {Returns line number of position within text.}
    text [string! binary!]
    position [string! binary! integer!]
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
