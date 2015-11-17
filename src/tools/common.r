REBOL [
    System: "Ren/C Core Extraction of the Rebol System"
    Title: "Common Routines for Tools"
    Rights: {
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren/C is Copyright 2015 MetaEducation
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "@HostileFork"
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        These are some common routines used by the utilities
        that build the system, which are found in %src/tools/
    }
]

;-- !!! BACKWARDS COMPATIBILITY: this does detection on things that have
;-- changed, in order to adapt the environment so that the build scripts
;-- can still work in older as well as newer Rebols.  Thus the detection
;-- has to be a bit "dynamic"

do %r2r3-future.r


;-- !!! switch to use spaces when code is transitioned
code-tab: (comment [rejoin [space space space space]] tab)

; http://stackoverflow.com/questions/11488616/
binary-to-c: func [
    {Converts a binary to a string of C source that represents an initializer
    for a character array.  To be "strict" C standard compatible, we do not
    use a string literal due to length limits (509 characters in C89, and
    4095 characters in C99).  Instead we produce an array formatted as
    '{0xYY, ...}' with 8 bytes per line}

    data [binary!]
    ; !!! Add variable name to produce entire 'const char *name = {...};' ?
     /local out str comma-count
] [
    out: make string! 6 * (length data)
    while [not tail? data] [
        append out code-tab 

        ;-- grab hexes in groups of 8 bytes
        hexed: enbase/base (copy/part data 8) 16
        data: skip data 8
        for-each [digit1 digit2] hexed [
            append out rejoin [{0x} digit1 digit2 {,} space]
        ]

        take/last out ;-- drop the last space
        if tail? data [
            take/last out ;-- lose that last comma
        ]
        append out newline ;-- newline after each group, and at end
    ]

    ;-- Sanity check (should be one more byte in source than commas out)
    parse out [(comma-count: 0) some [thru "," (++ comma-count)] to end]
    assert [(comma-count + 1) = (length head data)]

    out
]

;
; Rebol needs to bootstrap using old versions prior to having definitionally
; scoped returns implemented.  Hence don't assume passing a body with
; RETURN in it will return from the *caller*.  It will just wind up returning
; from *this loop wrapper* (in older Rebols) when the call is finished!
;
for-each-record-NO-RETURN: func [
    {Iterate a table with a header by creating an object for each row}
    'record [word!] {Word to set each time to the row made into an object}
    table [block!] {Table of values with header block as first element}
    body [block!] {Block to evaluate each time}
    /local headings result spec
] [
    unless block? first table [
        fail {Table of records does not start with a header block}
    ]
    headings: map-each word first table [
        unless word? word [
            fail [{Heading} word {is not a word}]
        ]
        to-set-word word
    ]

    table: next table

    set/any quote result: while [not empty? table] [
        if (length headings) > (length table) [
            fail {Element count isn't even multiple of header count}
        ]

        spec: collect [
            for-each column-name headings [
                keep column-name
                keep compose/only [quote (table/1)]
                table: next table
            ]
        ]

        set record make object! spec

        do body
    ]

    :result
]

find-record-unique: func [
    {Get a record in a table as an object, error if duplicate, none if absent}
    ;; return: [object! none!]
    table [block!] {Table of values with header block as first element}
    key [word!] {Object key to search for a match on}
    value {Value that the looked up key must be uniquely equal to}
    /local rec result
] [
    unless find first table key [
        fail [key {not found in table headers:} (first table)]
    ]

    result: none
    for-each-record-NO-RETURN rec table [
        unless value = select rec key [continue]

        if result [
            fail [{More than one table record matches} key {=} value]
        ]

        result: rec

        ; RETURN won't work.  We could break, but walk whole table to verify
        ; that it is well-formed.  (Here, correctness is more important.)
    ]
    result
]
