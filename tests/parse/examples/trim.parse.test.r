; %utrim.parse.test.r
;
; TRIM was initially native code, but was rewritten using PARSE3 by Brett
; Handley.  That version is currently in the Mezzanine.
;
; This file contains a preparatory port of that TRIM to UPARSE, to see what new
; features can be exploited and to further the testing surface.
;

[(
utrim: func [
    "Removes spaces from strings or blanks from blocks or objects"

    return: [any-string? any-list? blob! any-context?]
    series "Series (modified) or object (made)"
        [any-string? any-list? blob! any-context?]
    :head "Removes only from the head"
    :tail "Removes only from the tail"
    :auto "Auto indents lines relative to first line"
    :lines "Removes all line breaks and extra spaces"
    :all "Removes all whitespace"
    :with "Same as :ALL but removes specific characters"
        [char? text! blob! integer! block! bitset!]
][
    let all_TRIM: :all
    all: get $lib/all

    ; ACTION!s in the new object will still refer to fields in the original
    ; object.  That was the case in R3-Alpha as well.  Fixing it would require
    ; new kinds of binding overrides.  The feature itself is questionable.
    ;
    ; https://github.com/rebol/rebol-issues/issues/2288
    ;
    if any-context? series [
        if any [head tail auto lines all_TRIM with] [
            panic 'core/bad-refines
        ]
        trimmed: make (type of series) collect [
            for-each [key ^val] series [
                if not space? ^val [keep key]
            ]
        ]
        for-each [key val] series [
            poke trimmed key val
        ]
        return trimmed
    ]

    let rule
    case [
        any-list? series [
            if any [auto lines with] [
                ;
                ; Note: :WITH might be able to work, e.g. if it were a MAP!
                ; or BLOCK! of values to remove.
                ;
                panic 'core/bad-refines
            ]
            rule: blank

            if none [head tail] [
                head: tail: okay  ; plain utrim => utrim/HEAD/TAIL
            ]
        ]

        any-string? series [
            ; These are errors in the C version of utrim in R3-Alpha.
            ; One could question why :WITH implies :ALL
            ;
            if any [
                all [
                    auto
                    any [head tail lines]
                ]
                all [
                    any [all_TRIM with]
                    any [auto head tail lines]
                ]
            ][
                panic 'core/bad-refines
            ]

            rule: case [
                null? with [charset reduce [space tab]]
                bitset? with [with]
            ] else [
                charset with
            ]

            if any [all_TRIM lines head tail] [append rule newline]
        ]

        blob? series [
            if any [auto lines] [
                panic "Invalid refinements for utrim of BLOB!"
            ]

            rule: case [
                not with [#{00}]
                bitset? with [with]
            ] else [
                charset with
            ]

            if none [head tail] [
                head: tail: okay  ; plain utrim => utrim/HEAD/TAIL
            ]
        ]
    ] else [
        panic "Unsupported type passed to utrim"
    ]

    ; :ALL just removes all whitespace entirely.  No subtlety needed.
    ;
    if all_TRIM [
        parse series [opt some [remove rule | <next> | stop]]
        return series
    ]

    case:all [
        head [
            parse series [opt remove [some rule] to <end>]
        ]

        tail [
            parse series [opt some [remove [some rule <end>] | <next>]]  ; #2289
        ]
    ] then [
        return series
    ]

    assert [any-string? series]

    ; :LINES collapses all runs of whitespace down to just one space character
    ; with leading and trailing whitespace removed.
    ;
    if lines [
        parse series [opt some [change [some rule] (space) <next> | <next>]]
        if space = first series [take series]
        if space = last series [take:last series]
        return series
    ]

    ; UTRIM:AUTO measures first line indentation and removes indentation on
    ; later lines relative to that.  Only makes sense for ANY-STRING?, though
    ; a concept like "lines" could apply to a BLOCK! of BLOCK!s.
    ;
    let indent: #  ; by default, remove all indentation (opt in to the REPEAT)
    if auto [
        parse-thru series [
            ; Don't count empty lines, (e.g. utrim/auto {^/^/^/    asdf})
            opt remove [some LF]

            indent: measure opt some rule  ; length of spaces and tabs
        ]
    ]

    let line-start-rule: [opt remove repeat (indent) rule]

    parse series [
        line-start-rule
        opt some [
            not <end>
                ||
            ahead [opt some rule [newline | <end>]]
            remove [opt some rule]
            newline line-start-rule
                |
            <next>
        ]
    ]

    ; While trimming with /TAIL takes out any number of newlines, plain utrim
    ; in R3-Alpha and Red leaves at most one newline at the end.
    ;
    parse series [
        opt remove some newline
        opt some [newline remove [some newline <end>] | <next>]
    ]

    return series
]
ok)

    ; refinement order
    #83
    (equal?
        utrim:all:with "a" "a"
        utrim:with:all "a" "a"
    )

    #1948
    ("foo^/" = utrim "  foo ^/")

    (#{BFD3} = utrim #{0000BFD30000})
    (#{10200304} = utrim:with #{AEAEAE10200304BDBDBD} #{AEBD})

    (did s: copy "")

    ~bad-refines~ !! (utrim:auto:head s)
    ~bad-refines~ !! (utrim:auto:tail s)
    ~bad-refines~ !! (utrim:auto:lines s)
    ~bad-refines~ !! (utrim:auto:all s)
    ~bad-refines~ !! (utrim:all:head s)
    ~bad-refines~ !! (utrim:all:tail s)
    ~bad-refines~ !! (utrim:all:lines s)
    ~bad-refines~ !! (utrim:auto:with s -[*]-)
    ~bad-refines~ !! (utrim:head:with s -[*]-)
    ~bad-refines~ !! (utrim:tail:with s -[*]-)
    ~bad-refines~ !! (utrim:lines:with s -[*]-)

    (s = "")

    ("a  ^/  b  " = utrim:head "  a  ^/  b  ")
    ("  a  ^/  b" = utrim:tail "  a  ^/  b  ")
    ("foo^/^/bar^/" = utrim "  foo  ^/ ^/  bar  ^/  ^/  ")
    ("foobar" = utrim:all "  foo  ^/ ^/  bar  ^/  ^/  ")
    ("foo bar" = utrim:lines "  foo  ^/ ^/  bar  ^/  ^/  ")
    ("x^/" = utrim:auto "^/  ^/x^/")
    ("x^/" = utrim:auto "  ^/x^/")
    ("x^/ y^/ z^/" = utrim:auto "  x^/ y^/   z^/")
    ("x^/y" = utrim:auto "^/^/  x^/  y")

    ([a b] = utrim [a b])
    ([a b] = utrim [a b _])
    ([a b] = utrim [_ a b _])
    ([a _ b] = utrim [_ a _ b _])
    ([a b] = utrim:all [_ a _ b _])
    ([_ _ a _ b] = utrim:tail [_ _ a _ b _ _])
    ([a _ b _ _] = utrim:head [_ _ a _ b _ _])
    ([a _ b] = utrim:head:tail [_ _ a _ b _ _])
]
