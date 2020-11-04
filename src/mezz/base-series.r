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

reeval function [:terms [tag! set-word! <variadic>]] [
    n: 1
    while [<end> != w: take terms] [
        set w redescribe reduce [
            spaced [{Returns the} to word! w {value of a series}]
        ](
            specialize 'pick [picker: n]
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
    specialize adapt 'pick [
        picker: length of get lit location:
    ][
        picker: <removed-parameter>
    ]
)

;
; !!! End of functions that used to be natives, now mezzanine
;


repend: redescribe [
    "APPEND a reduced value to a series."
](
    adapt 'append [
        if set? 'value [
            value: reduce :value
        ]
    ]
)


; This is a userspace implementation of JOIN.  It is implemented on top of
; APPEND at the moment while it is being worked out, but since APPEND will
; fundamentally not operate on PATH! or TUPLE! it is going to be a bit
; inefficient.  However, it's easier to work it out as a userspace routine
; to figure out exactly what it should do, and make it a native later.
;
join: function [
    {Concatenates values to the end of a copy of a value}

    return:
        [any-series! issue! any-sequence! port!
            map! object! module! bitset!]
    base
        [any-series! issue! any-sequence! port!
            map! object! module! bitset!]
    value [<opt> any-value!]
][
    type: type of base  ; to set output type back to original if transformed
    case [
        find any-sequence! type [base: to block! base]
        issue! = type [base: to text! base]
        default [
            base: copy base
            type: _  ; don't apply any conversion at end
        ]
    ]

    result: switch type of :value [
        block! [append base reduce .identity :value]
        group! [
            fail 'base "Can't JOIN a GROUP! onto a series (use AS BLOCK!)."
        ]
        action! [
            fail 'base "Can't JOIN an ACTION! onto a series (use APPEND)."
        ]
        default [
            append/only base :value
        ]
    ]

    if type [
        result: as type result
    ]

    return result
]


; CHARSET was moved from "Mezzanine" because it is called by TRIM which is
; in "Base" - see TRIM.
;
charset: function [
    {Makes a bitset of chars for the parse function.}

    chars [text! block! binary! char! integer!]
    /length "Preallocate this many bits (must be > 0)"
        [integer!]
][
    init: either length [length] [[]]
    append make bitset! init chars
]


; TRIM is used by PORT! implementations, which currently rely on "Base" and
; not "Mezzanine", so this can't be in %mezz-series at the moment.  Review.
;
trim: function [
    {Removes spaces from strings or blanks from blocks or objects.}

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
    tail: :lib/tail
    head_TRIM: :head
    head: :lib/head
    all_TRIM: :all
    all: :lib/all

    ; ACTION!s in the new object will still refer to fields in the original
    ; object.  That was true in R3-Alpha as well.  Fixing this would require
    ; new kinds of binding overrides.  The feature itself is questionable.
    ;
    ; https://github.com/rebol/rebol-issues/issues/2288
    ;
    if any-context? series [
        if any [head_TRIM tail_TRIM auto lines all_TRIM with] [
            fail "Invalid refinements for TRIM of ANY-CONTEXT!"
        ]
        trimmed: make (type of series) collect [
            for-each [key val] series [
                if something? :val [keep key]
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
                fail "Invalid refinements for TRIM of ANY-ARRAY!"
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
                fail "Invalid refinements for TRIM of STRING!"
            ]

            rule: case [
                null? with [charset reduce [space tab]]
                bitset? with [with]
                default [charset with]
            ]

            if any [all_TRIM lines head_TRIM tail_TRIM] [append rule newline]
        ]

        binary? series [
            if any [auto lines] [
                fail "Invalid refinements for TRIM of BINARY!"
            ]

            rule: case [
                not with [#{00}]
                bitset? with [with]
                default [charset with]
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
        parse series [while [remove rule | skip | end break]]
        return series
    ]

    case/all [
        head_TRIM [
            parse series [remove [any rule] to end]
        ]

        tail_TRIM [
            parse series [while [remove [some rule end] | skip]]  ; #2289
        ]
    ] then [
        return series
    ]

    assert [any-string? series]

    ; /LINES collapses all runs of whitespace down to just one space character
    ; with leading and trailing whitespace removed.
    ;
    if lines [
        parse series [while [change [some rule] space skip | skip]]
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
        parse series [
            ; Don't count empty lines, (e.g. trim/auto {^/^/^/    asdf})
            remove [any LF]

            (indent: 0)
            s: some rule e:
            (indent: (index of e) - (index of s))

            end
        ]
    ]

    line-start-rule: compose/deep [
        remove [((if indent [[1 indent]] else ['any])) rule]
    ]

    parse series [
        line-start-rule
        any [
            while [
                ahead [any rule [newline | end]]
                remove [any rule]
                [newline line-start-rule]
                    |
                skip
            ]
        ]

        end
    ]

    ; While trimming with /TAIL takes out any number of newlines, plain TRIM
    ; in R3-Alpha and Red leaves at most one newline at the end.
    ;
    parse series [
        remove [any newline]
        while [newline remove [some newline end] | skip]
        end
    ]

    return series
]
