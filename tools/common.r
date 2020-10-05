REBOL [
    System: "Ren-C Core Extraction of the Rebol System"
    Title: "Common Routines for Tools"
    Rights: {
        Copyright 2012-2019 Ren-C Open Source Contributors
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

do %bootstrap-shim.r

do %make-file.r  ; Experimental!  Trying to replace PD_File...

; When you run a Rebol script, the `current-path` is the directory where the
; script is.  We assume that the Rebol source enlistment's root directory is
; one level above this file (which should be %tools/common.r)
;
repo: context [
    root: clean-path %../
    source-root: root
    tools: what-dir
]

spaced-tab: unspaced [space space space space]

to-c-name: function [
    {Take a Rebol value and transliterate it as a (likely) valid C identifier}

    return: [<opt> text!]
    value "Will be converted to text (via UNSPACED if BLOCK!)"
        [<blank> text! block! word!]
    /scope "#global or #local, see http://stackoverflow.com/questions/228783/"
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
        ; !!! Note: this was ... but that is now a TUPLE!  So this has to
        ; be changed for the moment.
        ;
        "~~~" [copy "ellipsis"]

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

        ; These are in the set of what are known as "alterative tokens".  They
        ; aren't exactly keywords (and in C they're just done with #define).
        ; Hence they are involved with the preproessor which means that
        ; "clever" macros like ARG(not) or REF(and) will be invoked as
        ; ARG(!) or REF(&&).  So instead use ARG(_not_) and REF(_and_), etc.
        ;
        ; (Complete list here for completeness, despite many being unused.)
        ;
        "and" [copy "_and_"]
        "and_eq" [copy "_and_eq_"]
        "bitand" [copy "_bitand_"]
        "bitor" [copy "_bitor_"]
        "compl" [copy "_compl_"]
        "not" [copy "_not_"]
        "not_eq" [copy "_not_eq_"]
        "or" [copy "_or_"]
        "or_eq" [copy "_or_eq_"]
        "xor" [copy "_xor_"]
        "xor_eq" [copy "_xor_eq_"]

        "did" [copy "_did_"]  ; Ren-C addition, replaces not-not `!!`

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

        find c-chars s/1 else [
            fail ["Non-alphanumeric or hyphen in" string "in to-c-name"]
        ]
    ]

    scope: default [#global]

    case [
        string/1 != "_" [<ok>]

        scope = 'global [
            fail "global C ids starting with _ are reserved"
        ]

        scope = 'local [
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
        ; grab hexes in groups of 8 bytes
        hexed: enbase/base (copy/part data 8) 16
        data: skip data 8
        for-each [digit1 digit2] hexed [
            append out unspaced [{0x} digit1 digit2 {,} space]
        ]

        take/last out  ; drop the last space
        if tail? data [
            take/last out  ; lose that last comma
        ]
        append out newline  ; newline after each group, and at end
    ]

    ; Sanity check (should be one more byte in source than commas out)
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

    while [not tail? table] [
        if (length of headings) > (length of table) [
            fail {Element count isn't even multiple of header count}
        ]

        spec: collect [
            for-each column-name headings [
                keep column-name
                keep compose/only [lit (table/1)]
                table: next table
            ]
        ]

        set var make object! spec
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
    if block? content [content: spaced content]
    content: to binary! content

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
    assert [dir? target]
    assert [dir? base]
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

    base: to-file delimit "/" base
    assert [dir? base]
    return base
]


stripload: function [
    {Get an equivalent to MOLD/FLAT (plus no comments) without using LOAD}

    return: "contents, w/o comments or indentation"
        [text!]
    source "Code to process without LOAD (avoids bootstrap scan differences)"
        [text! file!]
    /header "Request the header as text"
        [word! path!]  ; would be <output>, but that's not in bootstrap r3
    /gather "Collect what look like top-level declarations into variable"
        [word!]
][
    ; Removing spacing and comments from a Rebol file is not exactly trivial
    ; without LOAD.  But not impossible...and any tough cases in the mezzanine
    ; can be dealt with by hand.
    ;
    ; Note: This also removes newlines, which may not ultimately be desirable.
    ; The line numbering information, if preserved, could help correlate with
    ; lines in the original files.  That would require preserving some info
    ; about the file of origin, though.

    pushed: copy []  ; <Q>uoted or <B>raced string delimiter stack

    comment-or-space-rule: [
        ;
        ; Note: IF is deprecated in PARSE, and `:(...)` should be used instead
        ; once the bootstrap executable supports it.
        ;
        if (empty? pushed)  ; string not in effect, okay to proceed

        while [
            remove [some space]
            |
            ahead ";" remove [to [newline | end]]
        ]
    ]

    rule: [
        while [  ; https://github.com/rebol/rebol-issues/issues/1401
            newline [while [comment-or-space-rule remove newline]]
            |
            [ahead [any space ";"]] comment-or-space-rule
            |
            "^^{"  ; (actually `^{`) escaped brace, never count
            |
            "^^}"  ; (actually `^}`) escaped brace, never count
            |
            {^^"}  ; (actually `^"`) escaped quote, never count
            |
            "{" (if <Q> != last pushed [append pushed <B>])
            |
            "}" (if <B> = last pushed [take/last pushed])
            |
            {"} (
                case [
                    <Q> = last pushed [take/last pushed]
                    empty? pushed [append pushed <Q>]
                ]
            )
            |
            skip
        ]
        end
    ]

    either text? source [
        contents: source  ; useful for debugging STRIPLOAD from console
    ][
        text: as text! read source
        contents: find/tail text "^/]"
    ]

    ; This is a somewhat dodgy substitute for finding "top-level declarations"
    ; because it only finds things that look like SET-WORD! that are at the
    ; beginning of a line.  However, if we required the file to be LOAD-able
    ; by a bootstrap Rebol, that would create a dependency that would make
    ; it hard to use new scanner constructs in the mezzanine.
    ;
    ; Currently this is only used by the SYS context in order to generate top
    ; #define constants for easy access to the functions there.
    ;
    if gather [
        assert [block? get gather]
        append get gather collect [
            for-next t text [
                newline-pos: find t newline else [tail text]
                colon-pos: find/part t ":" newline-pos
                if unset? 'colon-pos [
                    t: newline-pos
                    continue
                ]
                space-pos: find/part t space colon-pos
                if set? 'space-pos [
                    t: newline-pos
                    continue
                ]
                str: copy/part t colon-pos
                all [
                    not find str ";"
                    not find str "{"
                    not find str "}"
                    not find str {"}
                    not find str "/"
                ] then [
                    keep as word! str
                ]
                t: newline-pos
            ]
        ]
    ]

    if header [
        if not hdr: copy/part (find/tail text "[") (find text "^/]") [
            fail ["Couldn't locate header in STRIPLOAD of" file]
        ]
        parse hdr rule else [
            fail ["STRIPLOAD failed to munge header of" file]
        ]
        set header hdr
    ]

    parse contents rule else [
        fail ["STRIPLOAD failed to munge contents of" file]
    ]

    if not empty? pushed [
        fail ["String delimiter stack imbalance while parsing" file]
    ]

    return contents
]
