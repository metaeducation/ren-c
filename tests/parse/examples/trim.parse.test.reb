; %utrim.parse.test.reb
;
; TRIM was initially native code, but was rewritten using PARSE3 by Brett
; Handley.  That version is currently in the Mezzanine.
;
; This file contains a preparatory port of that TRIM to UPARSE, to see what new
; features can be exploited and to further the testing surface.
;

[(
utrim: function [
    {Removes spaces from strings or blanks from blocks or objects.}

    return: [any-string! any-array! binary! any-context!]
    series "Series (modified) or object (made)"
        [any-string! any-array! binary! any-context!]
    /head "Removes only from the head"
    /tail "Removes only from the tail"
    /auto "Auto indents lines relative to first line"
    /lines "Removes all line breaks and extra spaces"
    /all "Removes all whitespace"
    /with "Same as /all, but removes specific characters"
        [char! text! binary! integer! block! bitset!]
][
    tail_TRIM: :tail
    tail: :lib.tail
    head_TRIM: :head
    head: :lib.head
    all_TRIM: :all
    all: :lib.all

    ; ACTION!s in the new object will still refer to fields in the original
    ; object.  That was true in R3-Alpha as well.  Fixing this would require
    ; new kinds of binding overrides.  The feature itself is questionable.
    ;
    ; https://github.com/rebol/rebol-issues/issues/2288
    ;
    if any-context? series [
        if any [head_TRIM tail_TRIM auto lines all_TRIM with] [
            fail ~bad-refines~
        ]
        trimmed: make (kind of series) collect [
            for-each [key val] series [
                if not blank? :val [keep key]
            ]
        ]
        for-each [key val] series [
            poke trimmed key :val
        ]
        return trimmed
    ]

    case [
        any-array? series [
            if any [auto lines with] [
                ;
                ; Note: /WITH might be able to work, e.g. if it were a MAP!
                ; or BLOCK! of values to remove.
                ;
                fail ~bad-refines~
            ]
            rule: blank!

            if not any [head_TRIM tail_TRIM] [
                head_TRIM: tail_TRIM: true  ; plain utrim => utrim/HEAD/TAIL
            ]
        ]

        any-string? series [
            ; These are errors raised by the C version of utrim in R3-Alpha.
            ; One could question why /with implies /all.
            ;
            if any [
                all [
                    auto
                    any [head_TRIM tail_TRIM lines]
                ]
                all [
                    any [all_TRIM with]
                    any [auto head_TRIM tail_TRIM lines]
                ]
            ][
                fail ~bad-refines~
            ]

            rule: case [
                null? with [charset reduce [space tab]]
                bitset? with [with]
            ] else [
                charset with
            ]

            if any [all_TRIM lines head_TRIM tail_TRIM] [append rule newline]
        ]

        binary? series [
            if any [auto lines] [
                fail "Invalid refinements for utrim of BINARY!"
            ]

            rule: case [
                not with [#{00}]
                bitset? with [with]
            ] else [
                charset with
            ]

            if not any [head_TRIM tail_TRIM] [
                head_TRIM: tail_TRIM: true  ; plain utrim => utrim/HEAD/TAIL
            ]
        ]
    ] else [
        fail "Unsupported type passed to utrim"
    ]

    ; /ALL just removes all whitespace entirely.  No subtlety needed.
    ;
    if all_TRIM [
        parse series [try some [remove rule | <any> | <end> stop]]
        return series
    ]

    case/all [
        head_TRIM [
            parse series [try remove [some rule] to <end>]
        ]

        tail_TRIM [
            parse series [try some [remove [some rule <end>] | <any>]]  ; #2289
        ]
    ] then [
        return series
    ]

    assert [any-string? series]

    ; /LINES collapses all runs of whitespace down to just one space character
    ; with leading and trailing whitespace removed.
    ;
    if lines [
        parse series [try some [change [some rule] (space) <any> | <any>]]
        if space = first series [take series]
        if space = last series [take/last series]
        return series
    ]

    ; UTRIM/AUTO measures first line indentation and removes indentation on
    ; later lines relative to that.  Only makes sense for ANY-STRING!, though
    ; a concept like "lines" could apply to a BLOCK! of BLOCK!s.
    ;
    indent: #  ; by default, remove all indentation (opt in to the REPEAT)
    if auto [
        parse- series [
            ; Don't count empty lines, (e.g. utrim/auto {^/^/^/    asdf})
            try remove [some LF]

            indent: measure try some rule  ; length of spaces and tabs
        ]
    ]

    line-start-rule: [try remove repeat (indent) rule]

    parse series [
        line-start-rule
        try some [
            not <end>
                ||
            ahead [try some rule [newline | <end>]]
            remove [try some rule]
            newline line-start-rule
                |
            <any>
        ]
    ]

    ; While trimming with /TAIL takes out any number of newlines, plain utrim
    ; in R3-Alpha and Red leaves at most one newline at the end.
    ;
    parse series [
        try remove some newline
        try some [newline remove [some newline <end>] | <any>]
    ]

    return series
]
true)

    ; refinement order
    #83
    (strict-equal?
        utrim/all/with "a" "a"
        utrim/with/all "a" "a"
    )

    #1948
    ("foo^/" = utrim "  foo ^/")

    (#{BFD3} = utrim #{0000BFD30000})
    (#{10200304} = utrim/with #{AEAEAE10200304BDBDBD} #{AEBD})

    (did s: copy {})

    ~bad-refines~ !! (utrim/auto/head s)
    ~bad-refines~ !! (utrim/auto/tail s)
    ~bad-refines~ !! (utrim/auto/lines s)
    ~bad-refines~ !! (utrim/auto/all s)
    ~bad-refines~ !! (utrim/all/head s)
    ~bad-refines~ !! (utrim/all/tail s)
    ~bad-refines~ !! (utrim/all/lines s)
    ~bad-refines~ !! (utrim/auto/with s {*})
    ~bad-refines~ !! (utrim/head/with s {*})
    ~bad-refines~ !! (utrim/tail/with s {*})
    ~bad-refines~ !! (utrim/lines/with s {*})

    (s = {})

    ("a  ^/  b  " = utrim/head "  a  ^/  b  ")
    ("  a  ^/  b" = utrim/tail "  a  ^/  b  ")
    ("foo^/^/bar^/" = utrim "  foo  ^/ ^/  bar  ^/  ^/  ")
    ("foobar" = utrim/all "  foo  ^/ ^/  bar  ^/  ^/  ")
    ("foo bar" = utrim/lines "  foo  ^/ ^/  bar  ^/  ^/  ")
    ("x^/" = utrim/auto "^/  ^/x^/")
    ("x^/" = utrim/auto "  ^/x^/")
    ("x^/ y^/ z^/" = utrim/auto "  x^/ y^/   z^/")
    ("x^/y" = utrim/auto "^/^/  x^/  y")

    ([a b] = utrim [a b])
    ([a b] = utrim [a b _])
    ([a b] = utrim [_ a b _])
    ([a _ b] = utrim [_ a _ b _])
    ([a b] = utrim/all [_ a _ b _])
    ([_ _ a _ b] = utrim/tail [_ _ a _ b _ _])
    ([a _ b _ _] = utrim/head [_ _ a _ b _ _])
    ([a _ b] = utrim/head/tail [_ _ a _ b _ _])
]
