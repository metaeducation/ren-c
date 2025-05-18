Rebol [
    system: "Ren-C Core Extraction of the Rebol System"
    title: "Common Routines for Tools"
    type: module
    name: Prep-Common
    rights: --[
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    version: 2.100.0
    needs: 2.100.100
    purpose: --[
        These are some common routines used by the utilities
        that build the system, which are found in %src/tools/
    ]--
]

import <bootstrap-shim.r>

; No specific verboseness/verbosity feature has been defined yet, but this
; points out places that could output more information if they wanted to.
;
export verbose: null

; When you run a Rebol script, the `current-path` is the directory where the
; script is.  We assume that the Rebol source enlistment's root directory is
; one level above this file (which should be %tools/common.r)
;
export repo-dir: clean-path %../

export spaced-tab: unspaced [space space space space]

export to-c-name: func [
    "Take a Rebol value and transliterate it as a (likely) valid C identifier"

    return: [null? text!]
    value "Will be converted to text (via UNSPACED if BLOCK!)"
        [<opt-out> text! block! word!]
    :scope "[#global #local #prefixed] see http://stackoverflow.com/q/228783/"
        [rune!]
][
    scope: default [#global]

    all [text? value, empty? value] then [
        panic:where ["TO-C-NAME received empty input"] 'value
    ]

    let c-chars: charset [
        #"a" - #"z"
        #"A" - #"Z"
        #"0" - #"9"
        #"_"
    ]

    let string: either block? value [unspaced value] [form value]

    switch string [
        ; Used specifically by t-routine.c to make SYM_ELLIPSIS
        ;
        "..." ["ellipsis_3"]

        ; Used to make SYM_HYPHEN which is needed by `charset [#"A" - #"Z"]`
        ;
        "-" ["hyphen_1"]

        ; Used to deal with the /? refinements (which may not last)
        ;
        "?" ["question_1"]

        ; None of these are used at present, but included just in case
        ;
        "*" ["asterisk_1"]
        "!" ["exclamation_1"]
        "+" ["plus_1"]
        "~" ["tilde_1"]
        "|" ["bar_1"]

        ; Special mechanics are required so that PATH! and TUPLE! and CHAIN!
        ; collapse to make these words:
        ;
        ;     >> compose $(_)/(_)
        ;     == /  ; a word
        ;
        "." ["dot_1"]
        "/" ["slash_1"]
        ":" ["colon_1"]

        ">" ["greater_1"]
        "<" ["lesser_1"]

        ; These are in the set of what are known as "alterative tokens".  They
        ; aren't exactly keywords (and in C they're just done with #define).
        ; Hence they are involved with the preproessor which means that
        ; "clever" macros like ARG(NOT) or Bool_ARG(AND) will be invoked as
        ; ARG(!) or Bool_ARG(&&).  So instead use ARG(_NOT_) and Bool_ARG(_AND_), etc.
        ;
        ; (Complete list here for completeness, despite many being unused.)
        ;
        "and" ["and_1"]
        "and_eq" ["and_eq_1"]
        "bitand" ["bitand_1"]
        "bitor" ["bitor_1"]
        "compl" ["compl_1"]
        "not" ["not_1"]
        "not_eq" ["not_eq_1"]
        "or" ["or_1"]
        "or_eq" ["or_eq_1"]
        "xor" ["xor_1"]
        "xor_eq" ["xor_eq_1"]

        "did" ["did_1"]  ; This is a macro in Ren-C code
    ] then s -> [
        return copy s
    ]

    ; If these symbols occur composite in a longer word, they use a shorthand.
    ; e.g. `foo?` => `foo_q`
    ;
    for-each [reb c] [
        #"'"  ""    ; isn't => isnt, didn't => didnt
        -   "_"     ; foo-bar => foo_bar
        *   "_p"    ; !!! because it symbolizes a (p)ointer in C??
        .   "_d"    ; (d)ot (only valid in [. .. ...] etc)
        /   "_s"    ; (s)lash (only valid in [/ // ///] etc)
        ?   "_q"    ; (q)uestion
        !   "_x"    ; e(x)clamation
        +   "_a"    ; (a)ddition
        |   "_b"    ; (b)ar
        >   "_g"    ; (g)reater
        <   "_l"    ; (l)esser
        =   "_e"    ; (e)qual
        #"^^"  "_c" ; (c)aret
        #"^^"  "_c" ; !!! Bug in shim loading requres a paired ^^ !!!
        #"@" "_z"   ; a was taken, doesn't make less sense than * => p
    ][
        replace string (form reb) c
    ]

    if empty? string [
        panic [
            "empty identifier produced by to-c-name for"
            (mold value) "of type" (to word! type of value)
        ]
    ]

    for-next 's string [
        all [
            scope <> #prefixed
            head? s
            pick charset [#"0" - #"9"] s.1
        ] then [
            panic ["identifier" string "starts with digit in to-c-name"]
        ]

        pick c-chars s.1 else [
            panic ["Non-alphanumeric or hyphen in" string "in to-c-name"]
        ]
    ]

    case [
        scope = #prefixed [<ok>]  ; assume legitimate prefix

        string.1 != #"_" [<ok>]

        scope = #global [
            panic ["global C ids starting with _ are reserved:" string]
        ]

        scope = #local [
            find charset [#"A" - #"Z"] string.2 then [
                panic [
                    "local C ids starting with _ and uppercase are reserved:"
                        string
                ]
            ]
        ]

        panic "/SCOPE must be #global or #local"
    ]

    return string
]


; http://stackoverflow.com/questions/11488616/
;
; 1. To be "strict" C standard compatible, we do not use a string literal due
;    to length limits (509 characters in C89, and 4095 characters in C99).
;    Instead we produce an array formatted as {0xYY, ...}, 8 bytes per line
;
; 2. There should be one more byte in source than commas out.
;
export binary-to-c: func [
    "Converts a binary to a string of C source to initialize a char array"

    return: [text!]
    data [blob!]
    :indent [integer!]
][
    let data-len: length of data

    let out: make text! 6 * (length of data)  ; array, not string literal [1]
    if indent [
        repeat indent [append out space]  ; no APPEND:DUP in bootstrap
    ]

    until [empty? opt data] [
        let hexed: enbase:base (copy:part data 8) 16
        data: skip data 8
        for-each [digit1 digit2] hexed [
            append out unspaced [-[0x]- digit1 digit2 -[,]- space]
        ]

        take:last out  ; drop the last space
        if empty? opt data [
            take:last out  ; lose that last comma
        ]
        append out newline  ; newline after each group, and at end
        if indent [
            repeat indent [append out space]  ; no APPEND:DUP in bootstrap
        ]
    ]

    let comma-count
    parse3 out [
        (comma-count: 0)
        some [thru "," (comma-count: comma-count + 1)]
        to <end>
    ]
    assert [(comma-count + 1) = data-len]  ; sanity check [2]

    return out
]


export parse-args: func [
    return: [block!]
    args [block!]
][
    let ret: make block! 4
    let standalone: make block! 4
    iterate (pin $args) [
        let name: null
        let value: args.1
        case [
            let idx: find value #"=" [; name=value
                name: to word! copy:part value (index of idx) - 1
                value: copy next idx
            ]
            #":" = last value [; name=value
                name: to word! copy:part value (length of value) - 1
                args: next args
                if empty? args [
                    panic ["Missing value after" value]
                ]
                value: args.1
            ]
        ]
        if all [  ; value1,value2,...,valueN
            not find value "["
            find value ","
        ][
            value: mold split value ","
        ]
        either name [
            append ret spread reduce [name value]
        ][  ; standalone-arg
            append standalone value
        ]
    ]
    if not empty? standalone [
        append ret '|
        append ret spread standalone
    ]
    return ret
]

export uppercase-of: func [
    "Copying variant of UPPERCASE, also FORMs words"
    string [text! word!]
][
    return uppercase form string
]

export lowercase-of: func [
    "Copying variant of LOWERCASE, also FORMs words"
    string [text! word!]
][
    return lowercase form string
]

export propercase: func [text [text!]] [
    assert [not empty? text]
    change text uppercase text.1
    let pos: next text
    while [pos: any [find pos "_", find pos "-"]] [
        if not pos: next pos [break]
        change pos uppercase pos.1
    ]
    return text
]

export propercase-of: func [
    "Make a copy of a string with just the first character uppercase"
    string [text! word!]
][
    return propercase form string
]

export write-if-changed: func [
    return: []
    dest [file!]
    content [text!]
][
    content: encode 'UTF-8 content  ; AS BLOB! locks in boot, can't clear

    any [
        not exists? dest
        content != read dest
    ] then [
        if verbose [
            print ["CHANGE DETECTED, WRITING =>" dest]
        ]
        write dest content
    ] else [
        if verbose [
            print ["NO CHANGE DETECTED:" dest]
        ]
    ]
]

export relative-to-path: func [
    return: [file!]
    target [file!]
    base [file!]
][
    assert [dir? target]
    assert [dir? base]
    target: split clean-path target "/"
    base: split clean-path base "/"
    if "" = last base [take:last base]
    while [all [
        not tail? target
        not tail? base
        base.1 = target.1
    ]] [
        base: next base
        target: next target
    ]
    iterate (pin $base) [base.1: %..]
    append base spread target

    base: to file! delimit "/" base
    assert [dir? base]
    return base
]


export stripload: func [
    "Get an equivalent to MOLD:FLAT (plus no comments) without using LOAD"

    return: "contents, w/o comments or indentation"
        [text!]
    source "Code to process without LOAD (avoids bootstrap scan differences)"
        [text! file!]
    :header "<output> Request the header as text"  ; no packs in bootstrap
        [word! path!]
    :gather "Collect what look like top-level declarations into variable"
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

    let pushed: copy []  ; <Q>uoted or <B>raced string delimiter stack

    let comment-or-space-rule: [
        cond (empty? pushed)  ; string not in effect, okay to proceed

        opt some [
            remove [some space]
            |
            ahead ";" remove [to [newline | <end>]]
        ]
    ]

    let rule: [
        opt some [
            newline [opt some [comment-or-space-rule remove newline]]
            |
            [ahead [opt some space ";"]] comment-or-space-rule
            |
            "^^{"  ; (actually `^{`) escaped brace, never count
            |
            "^^}"  ; (actually `^}`) escaped brace, never count
            |
            -[^^"]-  ; (actually `^"`) escaped quote, never count
            |
            "-[" (if <Q> != try last pushed [append pushed <B>])
            |
            "]-" (if <B> = try last pushed [take:last pushed])
            |
            -["]- (
                case [
                    <Q> = try last pushed [take:last pushed]
                    empty? pushed [append pushed <Q>]
                ]
            )
            |
            one
        ]
    ]

    let contents
    let file
    let text
    either text? source [
        contents: source  ; useful for debugging STRIPLOAD from console
        file: <textual source>
    ][
        text: as text! read source
        contents: next next find text "^/]"  ; /TAIL changed in new builds
        file: source
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
        append (ensure block! get gather) spread collect [
            for-next 't text [
                let newline-pos: find t newline else [tail of text]
                if not let colon-pos: find:part t ":" newline-pos [
                    t: newline-pos
                    continue
                ]
                if let space-pos: find:part t space colon-pos [
                    t: newline-pos
                    continue
                ]
                let str: copy:part t colon-pos
                if not parse3:match str [some "/"] [  ; symbols like ///:
                    if str.1 = #"/" [str: next str]  ; /foo: -> foo as name
                ]
                all [
                    not find str ";"
                    not find str "{"
                    not find str "}"
                    not find str -["]-
                    any [
                        not find str "/"
                        parse3:match str [some "/"]
                    ]
                    any [
                        not find str "."
                        parse3:match str [some "."]
                    ]
                ] then [
                    keep to word! str  ; AS WORD! must be at head
                ]
                t: newline-pos
            ]
        ]
    ]

    if header [
        if not let hdr: copy:part (next find text "[") (find text "^/]") [
            panic ["Couldn't locate header in STRIPLOAD of" file]
        ]
        parse3:match hdr rule else [
            panic ["STRIPLOAD failed to munge header of" file]
        ]
        set header hdr
    ]

    parse3:match contents rule else [
        panic ["STRIPLOAD failed to munge contents of" file]
    ]

    if not empty? pushed [
        panic ["String delimiter stack imbalance while parsing" file]
    ]

    return contents
]


; Some places (like SOURCES: in %make-spec.r for extensions) are permissive
; in terms of their format:
;
;     sources-A: %file.jpg
;
;     sources-B: [%file.jpg <some> <options>]
;
;     sources-C: [
;         %file1.jpg
;         [%file2.jpg <some> <options>]
;         %file3.jpg
;     ]
;
; It's a bit irregular, but convenient.  This function regularizes it:
;
;     sources-A: [
;         [%file.c]
;     ]
;
;     sources-B: [
;         [%file.c <some> <options>]
;     ]
;
;     sources-C: [
;         [%file1.c]
;         [%file2.c <some> <options>]
;         [%file3.c]
;     ]
;
export to-block-of-file-blocks: func [
    return: "Will be a top-level COPY of the block, or new block"
        [block!]
    x [<undo-opt> file! block!]
][
    if file? x [
        return reduce [blockify x]  ; case A
    ]
    any [null? x, x = []] then [
        return copy []
    ]
    if file? x.1 [
        all [
            not find (next x) file!
            not find (next x) block!
        ] then [
            return reduce [x]  ; case B
        ]
        ; fallthrough
    ]
    if find x tag! [  ; light check for mistakes
        panic [
            "FILE!/BLOCK! list can't contain TAG!s if multiple files:"
            mold:limit x 200
        ]
    ]
    return map-each 'item x [blockify item]  ; case C
]
