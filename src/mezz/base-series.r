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

first: redescribe [
    {Returns the first value of a series.}
](
    specialize 'pick [picker: 1]
)

second: redescribe [
    {Returns the second value of a series.}
](
    specialize 'pick [picker: 2]
)

third: redescribe [
    {Returns the third value of a series.}
](
    specialize 'pick [picker: 3]
)

fourth: redescribe [
    {Returns the fourth value of a series.}
](
    specialize 'pick [picker: 4]
)

fifth: redescribe [
    {Returns the fifth value of a series.}
](
    specialize 'pick [picker: 5]
)

sixth: redescribe [
    {Returns the sixth value of a series.}
](
    specialize 'pick [picker: 6]
)

seventh: redescribe [
    {Returns the seventh value of a series.}
](
    specialize 'pick [picker: 7]
)

eighth: redescribe [
    {Returns the eighth value of a series.}
](
    specialize 'pick [picker: 8]
)

ninth: redescribe [
    {Returns the ninth value of a series.}
](
    specialize 'pick [picker: 9]
)

tenth: redescribe [
    {Returns the tenth value of a series.}
](
    specialize 'pick [picker: 10]
)

last: func [
    {Returns the last value of a series.}
    return: [~null~ any-value!]
    value [any-series! tuple!]
][
    pick value length of value
]

;
; !!! End of functions that used to be natives, now mezzanine
;


; REPEND very literally does what it says, which is to reduce the argument
; and call APPEND.  This is not necessarily the most useful operation.
; Note that `x: 10 | repend [] 'x` would give you `[x]` in R3-Alpha
; and not 10.
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


join: func [
    "Concatenates values to the end of a string or path."
    return: [binary! any-string! path!]
    series [binary! any-string! path!]
    value [~void~ binary! any-string! path! word! integer!]
][
    if void? value [return copy series]
    return append/only copy series value
]

append-of: redescribe [
    "APPEND variation that copies the input series first."
](
    adapt 'append [
        series: copy series
    ]
)


; CHARSET was moved from "Mezzanine" because it is called by TRIM which is
; in "Base" - see TRIM.
;
charset: function [
    {Makes a bitset of chars for the parse function.}

    chars [text! block! binary! char! integer!]
    /length "Preallocate this many bits"
    len [integer!] "Must be > 0"
][
    ;-- CHARSET function historically has a refinement called /LENGTH, that
    ;-- is used to preallocate bits.  Yet the LENGTH? function has been
    ;-- changed to use just the word LENGTH.  We could change this to
    ;-- /CAPACITY SIZE or something similar, but keep it working for now.
    ;--
    length_CHARSET: length      ; refinement passed in
    length: ~                   ; helps avoid overlooking the ambiguity

    init: either length_CHARSET [len][[]]
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
    /with "Same as /all, but removes characters in 'str'"
    str [char! text! binary! integer! block! bitset!]
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
                head_TRIM: tail_TRIM: true ;-- plain TRIM => TRIM/HEAD/TAIL
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

            rule: either with [
                either bitset? str [str] [charset str]
            ][
                charset reduce [space tab]
            ]

            if any [all_TRIM lines head_TRIM tail_TRIM] [append rule newline]
        ]

        binary? series [
            if any [auto lines] [
                fail "Invalid refinements for TRIM of BINARY!"
            ]

            rule: either with [
                either bitset? str [str] [charset str]
            ][
                #{00}
            ]

            if not any [head_TRIM tail_TRIM] [
                head_TRIM: tail_TRIM: true ;-- plain TRIM => TRIM/HEAD/TAIL
            ]
        ]
    ] else [
        fail "Unsupported type passed to TRIM"
    ]

    ; /ALL just removes all whitespace entirely.  No subtlety needed.
    ;
    if all_TRIM [
        parse series [opt some [remove rule | skip | <end> break]]
        return series
    ]

    case/all [
        head_TRIM [
            parse series [remove [opt some rule] to <end>]
        ]

        tail_TRIM [
            parse series [opt some [remove [some rule <end>] | skip]] ;-- #2289
        ]
    ] then [
        return series
    ]

    assert [any-string? series]

    ; /LINES collapses all runs of whitespace down to just one space character
    ; with leading and trailing whitespace removed.
    ;
    if lines [
        parse series [opt some [change [some rule] space skip | skip]]
        if first series = space [take series]
        if last series = space [take/last series]
        return series
    ]

    ; TRIM/AUTO measures first line indentation and removes indentation on
    ; later lines relative to that.  Only makes sense for ANY-STRING!, though
    ; a concept like "lines" could apply to a BLOCK! of BLOCK!s.
    ;
    indent: _
    if auto [
        parse/match series [  ; !!! May not succeed, rules can mismatch
            ; Don't count empty lines, (e.g. trim/auto {^/^/^/    asdf})
            remove [opt some LF]

            (indent: 0)
            s: <here> some rule e: <here>
            (indent: (index of e) - (index of s))

            to <end>  ; !!! was just <end>, but didn't check success
        ]
    ]

    line-start-rule: case [
        not indent [
            [opt remove some rule]
        ]
        indent < 1 [
            []
        ]
        true [
            [remove repeat (reduce [1 indent]) rule]
        ]
    ]

    parse series [
        line-start-rule
        opt some [
            ahead [opt some rule [newline | <end>]]
            remove [opt some rule]
            [newline line-start-rule]
                |
            skip
        ]
    ]

    ; While trimming with /TAIL takes out any number of newlines, plain TRIM
    ; in R3-Alpha and Red leaves at most one newline at the end.
    ;
    parse series [
        remove [opt some newline]
        opt some [newline remove [some newline <end>] | skip]
    ]

    return series
]
