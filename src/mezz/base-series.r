REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Series Functions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {
        This code is evaluated just after actions, natives, sysobj, and other lower
        levels definitions. This file intializes a minimal working environment
        that is used for the rest of the boot.
    }
]

reeval lambda [:terms [tag! set-word! <variadic>]] [
    let n: 1
    let w
    while [<end> != w: take terms] [
        set w redescribe reduce [
            spaced [{Returns the} to word! w {value of a series}]
        ](
            specialize :pick [picker: n]
        )
        n: n + 1
    ]
]
    ; Variadic function so these words can be at top-level, module collects
    ;
    first: second: third: fourth: fifth:
    sixth: seventh: eighth: ninth: tenth:
    <end>

last: redescribe [
    {Returns the last value of a series.}
](
    specialize adapt :pick [
        picker: length of get 'location
    ][
        picker: <removed-parameter>
    ]
)

;
; !!! End of functions that used to be natives, now mezzanine
;


; This is a userspace implementation of JOIN.  It is implemented on top of
; APPEND at the moment while it is being worked out, but since APPEND will
; fundamentally not operate on PATH! or TUPLE! it is going to be a bit
; inefficient.  However, it's easier to work it out as a userspace routine
; to figure out exactly what it should do, and make it a native later.
;
; Historically, Redbol JOIN would implicitly reduce block arguments, and assume
; blocks should also be spliced.  Both aspects are removed from this JOIN, to
; bring it in line with the "as-is" defaults, and make it more useful for
; working with PATH!.
;
; JOIN does "path & tuple calculus" and makes sure the slashes or dots are
; correct:
;
;     >> join 'a spread [b c]
;     ** Error: you can't stick a to b without a /, nor b to c without a /
;
;     >> join 'a/ spread [b / c]
;     == a/b/c
;
; Note: `join ':a [b c]` => `:a/b/c` or `join [a] '/b/c` => [a]/b/c` may seem
; interesting and could occupy semantics open by illegal APPEND arguments...
; But anything that makes the result type not match the base type is likely
; to just cause confusion.  Weirdos who want features *like that* can make them
; but JOIN isn't the right place for it.
;
join: function [
    {Concatenates values to the end of a copy of a value}

    return:
        [any-series! issue! url! any-sequence! port!
            map! object! module! bitset!]
    base [
        datatype!
        any-series! issue! url!
        any-sequence!
        port! map! object! module! bitset!
    ]
    ^value [<void> any-value!]
][
    if void? unget value [
        return copy base
    ]

    ; !!! This doesn't do any "slash calculus" on URLs or files, e.g. to stop
    ; the append of two slashes in a row.  That is done by the MAKE-FILE code,
    ; and should be reviewed if it belongs here too.
    ;
    if match [url! issue! any-string!] base [
        return as (type of base) append (to text! base) unmeta value
    ]

    if not any-sequence? base [
        return append (copy base) unmeta value
    ]

    sep: either any-path? base ['/] ['.]
    type: type of base  ; to set output type back to original
    base: to block! base  ; TO operation copies

    if quasi? value [
        value: unquasi value
        if not block? value [
            fail "JOIN only accepts SPLICE blocks as ^META parameters"
        ]
    ]
    else [
        value: reduce [unquote value]  ; !!! should FOR-EACH take quoted?
    ]

    for-each item value [  ; !!! or REDUCE-EACH, for implicit reduce...?
        if blank? item [
            continue  ; old-rule, skips blanks
        ]
        any [
            any-sequence? item
            item = '.  ; !!! REVIEW
            item = '/
        ] then [
            case [
                item = sep [
                    if empty? base [  ; e.g. `join path! [/]`
                        append base blank
                        append base blank
                    ] else [
                        append base blank
                    ]
                ]
                (not blank? last base) and (not blank? first item) [
                    fail 'item [
                        "Elements must be separated with" sep
                    ]
                ]
                (blank? last base) and (not blank? first item) [
                    take/last base
                    append base spread as block! item
                ]
            ] else [
                if blank? first item [
                    append base spread next as block! item
                ] else [
                    append base spread as block! item
                ]
            ]
        ] else [
            case [
                empty? base [append base item]
                blank? last base [change back tail base item]
                fail 'item ["Elements must be separated with" sep]
            ]
        ]
    ]

    return as type base
]


; CHARSET was moved from "Mezzanine" because it is called by TRIM which is
; in "Base" - see TRIM.
;
charset: function [
    {Makes a bitset of chars for the parse function.}

    return: [bitset!]
    chars [text! block! binary! char! integer!]
    /length "Preallocate this many bits (must be > 0)"
        [integer!]
][
    init: either length [length] [[]]
    return append make bitset! init chars
]


; TRIM is used by PORT! implementations, which currently rely on "Base" and
; not "Mezzanine", so this can't be in %mezz-series at the moment.  Review.
;
trim: function [
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
                head_TRIM: tail_TRIM: true  ; plain TRIM => TRIM/HEAD/TAIL
            ]
        ]

        any-string? series [
            ; These are errors raised by the C version of TRIM in R3-Alpha.
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
                fail ~bad-refines~
            ]

            rule: case [
                not with [#{00}]
                bitset? with [with]
            ] else [
                charset with
            ]

            if not any [head_TRIM tail_TRIM] [
                head_TRIM: tail_TRIM: true  ; plain TRIM => TRIM/HEAD/TAIL
            ]
        ]
    ] else [
        fail "Unsupported type passed to TRIM"
    ]

    ; /ALL just removes all whitespace entirely.  No subtlety needed.
    ;
    if all_TRIM [
        parse3 series [opt some [remove rule | skip | <end> break]]
        return series
    ]

    case/all [
        head_TRIM [
            parse3 series [remove [opt some rule] to <end>]
        ]

        tail_TRIM [
            parse3 series [opt some [remove [some rule <end>] | skip]]  ; #2289
        ]
    ] then [
        return series
    ]

    assert [any-string? series]

    ; /LINES collapses all runs of whitespace down to just one space character
    ; with leading and trailing whitespace removed.
    ;
    if lines [
        parse3 series [opt some [change [some rule] (space) skip | skip]]
        if space = first series [take series]
        if space = last series [take/last series]
        return series
    ]

    ; TRIM/AUTO measures first line indentation and removes indentation on
    ; later lines relative to that.  Only makes sense for ANY-STRING!, though
    ; a concept like "lines" could apply to a BLOCK! of BLOCK!s.
    ;
    indent: _
    if auto [
        parse3* series [
            ; Don't count empty lines, (e.g. trim/auto {^/^/^/    asdf})
            remove [opt some LF]

            (indent: 0)
            s: <here>, some rule, e: <here>
            (indent: (index of e) - (index of s))
        ]
    ]

    line-start-rule: compose [
        remove (if indent '[opt [repeat (indent) rule]] else '[opt some rule])
    ]

    parse3 series [
        line-start-rule
        opt some [not <end> [
            ahead [opt some rule [newline | <end>]]
            remove [opt some rule]
            newline line-start-rule
                |
            skip
        ]]
    ]

    ; While trimming with /TAIL takes out any number of newlines, plain TRIM
    ; in R3-Alpha and Red leaves at most one newline at the end.
    ;
    parse3 series [
        opt remove [some newline]
        opt some [newline remove [some newline <end>] | skip]
    ]

    return series
]
