REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Series Functions"
    Rights: --{
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Note: --{
        This code is evaluated just after actions, natives, sysobj, and othe
        lower-level definitions. This file intializes a minimal working
        environment that is used for the rest of the boot.
    }--
]

run lambda [@terms [tag! set-run-word? <variadic>]] [
    let n: 1
    let w
    while [<end> != w: take terms] [
        w: resolve w
        set w redescribe reduce [
            spaced ["Returns the" w "value of a series"]
        ](
            specialize pick/ [picker: n]
        )
        n: n + 1
    ]
]
    ; Variadic function so these can be at top-level, module collects
    ;
    /first: /second: /third: /fourth: /fifth:
    /sixth: /seventh: /eighth: /ninth: /tenth:
    <end>

last: redescribe [
    "Returns the last value of a series"
](
    specialize adapt pick/ [
        picker: length of :location
    ][
        picker: <removed-parameter>
    ]
)

;
; !!! End of functions that used to be natives, now mezzanine
;

; CHARSET was moved from "Mezzanine" because it is called by TRIM which is
; in "Base" - see TRIM.
;
/charset: func [
    "Makes a bitset of chars for the parse function"

    return: [bitset!]
    chars [text! block! blob! char? integer!]
    :length "Preallocate this many bits (must be > 0)"
        [integer!]
][
    let init: either length [length] [[]]
    return append make bitset! init chars
]


; TRIM is used by PORT! implementations, which currently rely on "Base" and
; not "Mezzanine", so this can't be in %mezz-series at the moment.  Review.
;
/trim: func [
    "Removes spaces from strings or blanks from blocks or objects"

    return: [any-string? any-list? blob! any-context?]
    series "Series (modified) or object (made)"
        [any-string? any-list? blob! any-context?]
    :head "Removes only from the head"
    :tail "Removes only from the tail"
    :auto "Auto indents lines relative to first line"
    :lines "Removes all line breaks and extra spaces"
    :all "Removes all whitespace"
    :with "Same as :ALL, but removes specific characters"
        [char? text! blob! integer! block! bitset!]
][
    let all_TRIM: all
    all: lib/all/

    ; ACTION!s in the new object will still refer to fields in the original
    ; object.  That was true in R3-Alpha as well.  Fixing this would require
    ; new kinds of binding overrides.  The feature itself is questionable.
    ;
    ; https://github.com/rebol/rebol-issues/issues/2288
    ;
    if any-context? series [
        if any [head tail auto lines all_TRIM with] [
            fail 'core/bad-refines
        ]
        trimmed: make (type of series) collect [
            for-each [key val] series [
                if not blank? :val [keep key]
            ]
        ]
        for-each [key val] series [
            poke trimmed key :val
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
                fail 'core/bad-refines
            ]
            rule: blank!

            if not any [head tail] [
                head: tail: #  ; plain TRIM => TRIM:HEAD:TAIL
            ]
        ]

        any-string? series [
            ; These are errors raised by the C version of TRIM in R3-Alpha.
            ; One could question why :with implies :all.
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
                fail 'core/bad-refines
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
                fail 'core/bad-refines
            ]

            rule: case [
                not with [#{00}]
                bitset? with [with]
            ] else [
                charset with
            ]

            if not any [head tail] [
                head: tail: #  ; plain TRIM => TRIM:HEAD:TAIL
            ]
        ]
    ] else [
        fail "Unsupported type passed to TRIM"
    ]

    ; /ALL just removes all whitespace entirely.  No subtlety needed.
    ;
    if all_TRIM [
        parse3 series [opt some [remove rule | one | <end> break]]
        return series
    ]

    case:all [
        head [
            parse3 series [remove [opt some rule] to <end>]
        ]

        tail [
            parse3 series [opt some [remove [some rule <end>] | one]]  ; #2289
        ]
    ] then [
        return series
    ]

    assert [any-string? series]

    ; /LINES collapses all runs of whitespace down to just one space character
    ; with leading and trailing whitespace removed.
    ;
    if lines [
        parse3 series [opt some [change [some rule] (space) one | one]]
        if space = first series [take series]
        if space = last series [take:last series]
        return series
    ]

    ; TRIM:AUTO measures first line indentation and removes indentation on
    ; later lines relative to that.  Only makes sense for ANY-STRING?, though
    ; a concept like "lines" could apply to a BLOCK! of BLOCK!s.
    ;
    let indent: null
    let s
    let e
    if auto [
        parse3 series [
            ; Don't count empty lines, (e.g. trim:auto -{^/^/^/    asdf}-)
            opt remove some LF

            (indent: 0)
            s: <here>, opt some rule, e: <here>
            (indent: (index of e) - (index of s))

            accept (~)  ; don't need to reach end
        ]
    ]

    let line-start-rule: compose [
        remove (if indent '[opt [repeat (indent) rule]] else '[opt some rule])
    ]

    parse3 series [
        line-start-rule
        opt some [not <end> [
            ahead [opt some rule [newline | <end>]]
            remove [opt some rule]
            newline line-start-rule
                |
            one
        ]]
    ]

    ; While trimming with /TAIL takes out any number of newlines, plain TRIM
    ; in R3-Alpha and Red leaves at most one newline at the end.
    ;
    parse3 series [
        opt remove [some newline]
        opt some [newline remove [some newline <end>] | one]
    ]

    return series
]
