REBOL [
    System: "Ren-C Core Extraction of the Rebol System"
    Title: "Common Routines for Tools"
    Rights: {
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        These are some common routines used by the utilities
        that build the system, which are found in %src/tools/
    }
]

; !!! This file does not include the backwards compatibility %r2r3-future.r.
; The reason is that some code assumes it is running Ren-C, and that file
; disables features which are not backward compatible, which shouldn't be
; disabled if you *are* running Ren-C (e.g. the tests)


; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
===: func [:remarks [any-value! <...>]] [
    until [
        equal? '=== take remarks
    ]
]


;; Repository meta data.
;; - Good for keeping fixed paths out of scripts.
;;

repo: context [
    root: clean-path %../../
    source-root: root
    tools: what-dir
]

;;

spaced-tab: unspaced [space space space space]

to-c-name: function [
    {Take a Rebol value and transliterate it as a (likely) valid C identifier}

    return: [text!]
    value "Will be converted to text (via UNSPACED if BLOCK!)"
        [text! block! word!]
    /scope "See C's rules: http://stackoverflow.com/questions/228783/"
    where "Either #global or #local (defaults global)"
        [issue!]
][
    all [
        text? value
        empty? value
    ] then [
        fail/where ["TO-C-NAME received empty input"] 'value
    ]

    c-chars: charset [
        #"a" - #"z"
        #"A" - #"Z"
        #"0" - #"9"
        #"_"
    ]

    string: either block? :value [unspaced value] [form value]

    string: switch string [
        ; Used specifically by t-routine.c to make SYM_ELLIPSIS
        ;
        "..." [copy "ellipsis"]

        ; Used to make SYM_HYPHEN which is needed by `charset [#"A" - #"Z"]`
        ;
        "-" [copy "hyphen"]

        ; Used to deal with the /? refinements (which may not last)
        ;
        "?" [copy "q"]

        ; None of these are used at present, but included just in case
        ;
        "*" [copy "asterisk"]
        "." [copy "period"]
        "!" [copy "exclamation"]
        "+" [copy "plus"]
        "~" [copy "tilde"]
        "|" [copy "bar"]

        default [
            ;
            ; If these symbols occur composite in a longer word, they use a
            ; shorthand; e.g. `foo?` => `foo_q`

            for-each [reb c] [
              #"'"  ""      ; isn't => isnt, don't => dont 
                -   "_"     ; foo-bar => foo_bar
                *   "_p"    ; !!! because it symbolizes a (p)ointer in C??
                .   "_"     ; !!! same as hyphen?
                ?   "_q"    ; (q)uestion
                !   "_x"    ; e(x)clamation
                +   "_a"    ; (a)ddition
                ~   "_t"    ; (t)ilde
                |   "_b"    ; (b)ar

            ][
                replace/all string (form reb) c
            ]

            string
        ]
    ]

    if empty? string [
        fail [
            "empty identifier produced by to-c-name for"
            (mold value) "of type" (mold type of value)
        ]
    ]

    repeat s string [
        (head? s) and [find charset [#"0" - #"9"] s/1] and [
            fail ["identifier" string "starts with digit in to-c-name"]
        ]

        find c-chars s/1 or [
            fail ["Non-alphanumeric or hyphen in" string "in to-c-name"]
        ]
    ]

    where: default [#global]

    case [
        string/1 != "_" [<ok>]

        where = 'global [
            fail "global C ids starting with _ are reserved"
        ]

        where = 'local [
            find charset [#"A" - #"Z"] string/2 then [
                fail "local C ids starting with _ and uppercase are reserved"
            ]
        ]

        fail "/SCOPE must be #global or #local"
    ]

    return string
]


; http://stackoverflow.com/questions/11488616/
binary-to-c: function [
    {Converts a binary to a string of C source that represents an initializer
    for a character array.  To be "strict" C standard compatible, we do not
    use a string literal due to length limits (509 characters in C89, and
    4095 characters in C99).  Instead we produce an array formatted as
    '{0xYY, ...}' with 8 bytes per line}

    return: [text!]
    data [binary!]
][
    out: make text! 6 * (length of data)
    while [not tail? data] [
        ;-- grab hexes in groups of 8 bytes
        hexed: enbase/base (copy/part data 8) 16
        data: skip data 8
        for-each [digit1 digit2] hexed [
            append out unspaced [{0x} digit1 digit2 {,} space]
        ]

        take/last out ;-- drop the last space
        if tail? data [
            take/last out ;-- lose that last comma
        ]
        append out newline ;-- newline after each group, and at end
    ]

    ;-- Sanity check (should be one more byte in source than commas out)
    parse out [
        (comma-count: 0)
        some [thru "," (comma-count: comma-count + 1)]
        to end
    ]
    assert [(comma-count + 1) = (length of head of data)]

    out
]


for-each-record: function [
    {Iterate a table with a header by creating an object for each row}

    return: [<opt> any-value!]
    'var "Word to set each time to the row made into an object record"
        [word!]
    table "Table of values with header block as first element"
        [block!]
    body "Block to evaluate each time"
        [block!]
][
    if not block? first table [
        fail {Table of records does not start with a header block}
    ]

    headings: map-each word first table [
        if not word? word [
            fail [{Heading} word {is not a word}]
        ]
        as set-word! word
    ]

    table: next table

    while-not [tail? table] [
        if (length of headings) > (length of table) [
            fail {Element count isn't even multiple of header count}
        ]

        spec: collect [
            for-each column-name headings [
                keep column-name
                keep compose/only [quote (table/1)]
                table: next table
            ]
        ]

        set var has spec
        do body
    ]
]


find-record-unique: function [
    {Get a record in a table as an object, error if duplicate, blank if absent}
    
    return: [<opt> object!]
    table [block!]
        {Table of values with header block as first element}
    key [word!]
        {Object key to search for a match on}
    value
        {Value that the looked up key must be uniquely equal to}
][
    if not find first table key [
        fail [key {not found in table headers:} (first table)]
    ]

    result: _
    for-each-record rec table [
        if value <> select rec key [continue]

        if result [
            fail [{More than one table record matches} key {=} value]
        ]

        ; Could break, but walk whole table to verify that it is well-formed.
    ]
    return opt result
]


parse-args: function [
    args
][
    ret: make block! 4
    standalone: make block! 4
    args: any [args copy []]
    if not block? args [args: split args [some " "]]
    iterate args [
        name: _
        value: args/1
        case [
            idx: find value #"=" [; name=value
                name: to word! copy/part value (index of idx) - 1
                value: copy next idx
            ]
            #":" = last value [; name=value
                name: to word! copy/part value (length of value) - 1
                args: next args
                if empty? args [
                    fail ["Missing value after" value]
                ]
                value: args/1
            ]
        ]
        if all [; value1,value2,...,valueN
            not find value "["
            find value ","
        ][value: mold split value ","]
        either name [
            append ret reduce [name value]
        ][; standalone-arg
            append standalone value
        ]
    ]
    if empty? standalone [return ret]
    append ret '|
    append ret standalone
]

fix-win32-path: func [
    path [file!]
    <local> letter colon
][
    if 3 != fourth system/version [return path] ;non-windows system

    drive: first path
    colon: second path

    all [
        any [
            (#"A" <= drive) and [#"Z" >= drive]
            (#"a" <= drive) and [#"z" >= drive]
        ]
        #":" = colon
    ] then [
        insert path #"/"
        remove skip path 2 ;remove ":"
    ]

    path
]

uppercase-of: func [
    {Copying variant of UPPERCASE, also FORMs words}
    string [text! word!]
][
    uppercase form string
]

lowercase-of: func [
    {Copying variant of LOWERCASE, also FORMs words}
    string [text! word!]
][
    lowercase form string
]

propercase: func [value] [uppercase/part (copy value) 1]

propercase-of: func [
    {Make a copy of a string with just the first character uppercase}
    string [text! word!]
][
    propercase form string
]

write-if-changed: function [
    return: <void>
    dest [file!]
    content [text! block!]
][
    if block? content [
        content: spaced content
    ]

    if not binary? content [
        content: to binary! content
    ]

    any [
        not exists? dest
        content != read dest
    ] then [
        write dest content
    ]
]

relative-to-path: func [
    target [file!]
    base [file!]
][
    target: split clean-path target "/"
    base: split clean-path base "/"
    if "" = last base [take/last base]
    while [all [
        not tail? target
        not tail? base
        base/1 = target/1
    ]] [
        base: next base
        target: next target
    ]
    iterate base [base/1: %..]
    append base target
    to-file delimit base "/"
]

