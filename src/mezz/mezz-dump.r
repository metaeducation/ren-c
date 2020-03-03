REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Dump"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

dump: function [
    {Show the name of a value or expressions with the value (See Also: --)}

    return: "Doesn't return anything, not even void (so like a COMMENT)"
        []
    :value [any-value!]
    :extra "Optional variadic data for SET-WORD!, e.g. `dump x: 1 + 2`"
        [any-value! <...>]
    /prefix "Put a custom marker at the beginning of each output line"
    sigil [text!]

    <static> enablements (make map! [])
][
    print: adapt 'lib/print [
        if set? 'sigil [
            if select enablements sigil <> #on [return]
            write-stdout sigil
            write-stdout space
        ]
    ]

    val-to-text: function [return: [text!] val [<opt> any-value!]] [
        case [
            not set? 'val ["// null"]
            object? :val [unspaced ["make object! [" (dump-obj val) "]"]]
            default [mold/limit :val system/options/dump-size]
        ]
    ]

    dump-one: function [return: <void> item] [
        switch type of item [
            refinement! ;-- treat as label, /a no shift and shorter than "a"
            text! [ ;-- good for longer labeling when you need spaces/etc.
                print [mold/limit item system/options/dump-size]
            ]

            word! [
                print [to set-word! item | val-to-text get item]
            ]

            path! [
                print [to set-path! item | val-to-text reduce item]
            ]

            group! [
                print [unspaced [mold item ":"] | mold eval item]
            ]

            issue! [
                enablements/(sigil): item
            ]

            fail/where [
                "Item not TEXT!, INTEGER!, WORD!, PATH!, GROUP!:" :item
            ] 'value
        ]
    ]

    case [
        swp: match [set-word! set-path!] :value [ ; `dump x: 1 + 2`
            pos: evaluate/set extra (quote result:)
            set swp :result
            print [swp | result]
        ]

        b: match block! :value [
            while [not tail? b] [
                if swp: match [set-word! set-path!] :b/1 [ ; `dump [x: 1 + 2]`
                    b: evaluate/set b (quote result:)
                    print [swp | result]
                ] else [
                    dump-one b/1
                    b: next b
                ]
            ]
        ]

        default [dump-one :value]
    ]
]

contains-newline: function [return: [logic!] pos [block! group!]] [
    while [pos] [
        any [
            new-line? pos
            (match [block! group!] :pos/1) and [contains-newline :pos/1]
        ] then [return true]

        pos: try next pos
    ]
    return false
]

dump-to-newline: adapt 'dump [
    if not tail? extra [
        ;
        ; Mutate VARARGS! into a BLOCK!, with passed-in value at the head
        ;
        value: reduce [:value]
        while-not [new-line? extra or [tail? extra] or [bar? extra/1]] [
            append/only value extra/1
            if (match [block! group!] :extra/1) and [contains-newline :extra/1] [
                break
            ]
            take extra
        ]
        extra: make varargs! [] ;-- don't allow more takes
    ]
]

dumps: enfix function [
    {Fast generator for dumping function that uses assigned name for prefix}

    return: [action!]
    :name [set-word!]
    :value "If issue, create non-specialized dumper...#on or #off by default"
        [issue! text! integer! word! set-word! set-path! group! block!]
    extra "Optional variadic data for SET-WORD!, e.g. `dv: dump var: 1 + 2`"
        [<opt> any-value! <...>]
][
    if issue? value [
        d: specialize 'dump-to-newline [sigil: as text! name]
        if value <> #off [d #on] ;-- note: d hard quotes its argument
    ] else [
        ; Make it easy to declare and dump a variable at the same time.
        ;
        if match [set-word! set-path!] value [
            evaluate/set extra value
            value: either set-word? value [as word! value] [as path! value]
        ]

        ; No way to enable/disable full specializations unless there is
        ; another function or a refinement.  Go with wrapping and adding
        ; refinements for now.
        ;
        ; !!! This actually can't work as invisibles with refinements do not
        ; have a way to be called--in spirit they are like enfix functions,
        ; so SHOVE (->) would be used, but it doesn't work yet...review.)
        ;
        d: function [return: [] /on /off <static> d'] compose/deep [
            d': default [
                d'': specialize 'dump [sigil: (as text! name)]
                d'' #on
            ]
            case [
                on [d' #on]
                off [d' #off]
                default [d' (value)]
            ]
        ]
    ]
    set name :d
]

; Handy specialization for dumping, prefer to DUMP when doing temp output
;
; !!! What should `---` and `----` do?  One fairly sensible idea would be to
; increase the amount of debug output, as if a longer dash meant "give me
; more output".  They could also be lower and lower verbosity levels, but
; verbosity could also be cued by an INTEGER!, e.g. `-- 2 [x y]`
;
--: dumps #on


; !!! R3-Alpha labeled this "MOVE THIS INTERNAL FUNC" but it is actually used
; to search for patterns in HELP when you type in something that isn't bound,
; so it uses that as a string pattern.  Review how to better factor that
; (as part of a general help review)
;
dump-obj: function [
    "Returns a block of information about an object or port."
    return: [<opt> block!]
    obj [object! port!]
    /match "Include only those that match a string or datatype" pat
][
    clip-str: func [str] [
        ; Keep string to one line.
        trim/lines str
        if (length of str) > 48 [str: append copy/part str 45 "..."]
        str
    ]

    form-val: func [val [any-value!]] [
        ; Form a limited string from the value provided.
        if any-array? :val [return spaced ["length:" length of val]]
        if image? :val [return spaced ["size:" val/size]]
        if datatype? :val [return form val]
        if action? :val [
            return clip-str any [title-of :val | mold spec-of :val]
        ]
        if object? :val [val: words of val]
        if typeset? :val [val: make block! val]
        if port? :val [val: reduce [val/spec/title val/spec/ref]]
        if gob? :val [return spaced ["offset:" val/offset "size:" val/size]]
        clip-str mold :val
    ]

    form-pad: func [val size] [
        ; Form a value with fixed size (space padding follows).
        val: form val
        insert/dup tail of val #" " size - length of val
        val
    ]

    ; Search for matching strings:
    return collect [
        wild: did all [set? 'pat | text? pat | find pat "*"]

        for-each [word val] obj [
            if not set? 'val [continue] ;-- !!! review

            type: type of :val

            str: if lib/match [action! object!] :type [
                spaced [word | mold spec-of :val | words of :val]
            ] else [
                form word
            ]

            if any [
                not match
                all [
                    set? 'val
                    either text? :pat [
                        either wild [
                            tail? (find/any/match str pat else [pat])
                        ][
                            find str pat
                        ]
                    ][
                        all [
                            datatype? get :pat
                            type = get :pat
                        ]
                    ]
                ]
            ][
                str: form-pad word 15
                append str #" "
                append str form-pad type 10 - ((length of str) - 15)
                keep spaced [
                    "  " str
                    if type [form-val :val]
                    newline
                ]
            ]
        ]
    ]
]

; Invisible (like a comment) but takes data until end of line -or- end of
; the input stream:
;
;     ** this 'is <commented> [out]
;     print "This is not"
;
;     (** this 'is <commented> [out]) print "This is not"
;
;     ** this 'is (<commented>
;       [out]
;     ) print "This is not"
;
; Notice that if line breaks occur internal to an element on the line, that
; is detected, and lets that element be the last commented element.
;
**: enfix function [
    {Comment until end of line, or end of current BLOCK!/GROUP!}

    return: []
    left "Enfix required for 'fully invisible' enfix behavior (ignored)"
        [<opt> <end> any-value!]
    :args [any-value! <...>]
][
    while [(not new-line? args) and [value: take args]] [
        all [
            any-array? :value
            contains-newline value
            return
        ]
    ]
]
