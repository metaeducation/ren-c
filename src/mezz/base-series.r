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
    loop [<end> != w: take terms] [
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
; JOIN does "path & tuple calculus" and makes sure the slashes or dots are
; correct.  BLANK!s do not have meaning and are discarded, while values cannot
; be joined against each other without slashes:
;
;     >> join path! [a b c]
;     ** Error: you can't stick a to b without a /, nor b to c without a /
;
;     >> join path! [a/ b / c]
;     == a/b/c
;
;     >> join 'a/ [_ _ _ b]
;     == a/b
;
; Note: `join ':a [b c]` => `:a/b/c` or `join [a] '/b/c` => [a]/b/c might seem
; interesting and could occupy sematnics left open by illegal APPEND arguments.
; But anything that makes the result type not match the base type is likely
; to just cause confusion.  Weirdos who want features *like that* can make them
; but JOIN isn't the right place for it.
;
join: function [
    {Concatenates values to the end of a copy of a value}

    return:
        [any-series! issue! any-sequence! port!
            map! object! module! bitset!]
    base [
        datatype!
        any-series! issue!
        any-sequence!
        port! map! object! module! bitset!
    ]
    value [<opt> any-value!]
][
    if blank? :value [
        return copy base  ; can't be <blank> because that returns NULL
    ]

    type: type of base  ; to set output type back to original if transformed
    case [
        type = datatype! [
            type: base
            case [
                find any-sequence! type [base: copy []]
                find any-array! type [base: copy []]
                find any-string! type [base: copy ""]
                type = issue! [base: copy ""]
                type = binary! [base: copy #{}]

                fail ["Invalid datatype for JOIN:" type]
            ]
        ]
        find any-sequence! type [base: to block! base]
        issue! = type [base: to text! base]
    ] else [
        base: copy base
        type: _  ; don't apply any conversion at end
    ]

    result: switch type of :value [
        block! [
            if find any-sequence! type [  ; want slash or dot "calculus"
                sep: either find any-path! type ['/] ['.]
                for-each item value [
                    if blank? item [
                        continue  ; blanks skipped, use / or . to get "blanks"
                    ]
                    if not find any-sequence! kind of item [
                        case [
                            empty? base [append base ^item]
                            _ = last base [change back tail base ^item]
                            fail @item ["Elements must be separated with" sep]
                        ]
                    ] else [
                        case [
                            item = sep [
                                if empty? base [  ; e.g. `join path! [/]`
                                    append base ^blank
                                    append base ^blank
                                ] else [
                                    append base ^blank
                                ]
                            ]
                            (last base) and (first item) [
                                fail @item [
                                    "Elements must be separated with" sep
                                ]
                            ]
                        ] else [
                            if _ = first item [
                                append base next as block! item
                            ] else [
                                append base as block! item
                            ]
                        ]
                    ]
                ]
            ]
            else [
                append base value
            ]
        ]
    ] else [
        if find any-sequence! type [
            if find any-sequence! kind of value [
                if not match [path! tuple!] value [
                    fail "Can only append plain PATH! and TUPLE! to sequences"
                ]
                if type = type of value [  ; merging scenario
                    all [last base, first value] then [
                        fail "Elements must be separated with / or ."
                    ]
                    value: as block! value

                    ; `(join 'a/ '/b)` needs to make a block [a _ b]
                    ; But if there's a one sided slash there needs to be no
                    ; blank between, it's a normal path.
                    ;
                    all [not last base, not first value] then [
                        take/last base
                    ] else [
                        if not last base [take/last base]
                        if not first value [value: next value]  ; locked
                    ]
                ]
            ] else [
                if not quoted? value [
                    value: quote value  ; !!! allows `join 'a/ 'b`, good idea?
                ]
                if last base [
                    fail "Elements must be separated with / or ."
                ]
                take/last base
            ]
        ]
        append base :value
    ]

    if type [
        return as type base
    ]

    return base
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
            ] else [
                charset with
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
        parse series [while [remove rule | skip | end break]]
        return series
    ]

    case/all [
        head_TRIM [
            parse series [remove [while rule] to end]
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
        parse series [while [change [some rule] (space) skip | skip]]
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
        parse* series [
            ; Don't count empty lines, (e.g. trim/auto {^/^/^/    asdf})
            remove [while LF]

            (indent: 0)
            s: here, some rule, e: here
            (indent: (index of e) - (index of s))
        ]
    ]

    line-start-rule: compose [
        remove (if indent '[opt [repeat (indent) rule]] else '[while rule])
    ]

    parse series [
        line-start-rule
        while [not end [
            ahead [while rule [newline | end]]
            remove [while rule]
            newline line-start-rule
                |
            skip
        ]]
    ]

    ; While trimming with /TAIL takes out any number of newlines, plain TRIM
    ; in R3-Alpha and Red leaves at most one newline at the end.
    ;
    parse series [
        remove [while newline]
        while [newline remove [some newline end] | skip]
    ]

    return series
]
