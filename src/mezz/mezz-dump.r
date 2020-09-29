REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Dump"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Ren-C Open Source Contributors
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
        [any-value! <variadic>]
    /prefix "Put a custom marker at the beginning of each output line"
        [text!]

    <static> enablements (make map! [])
][
    print: adapt 'lib/print [
        if prefix [
            if select enablements prefix <> #on [return]
            write-stdout prefix
            write-stdout space
        ]
    ]

    val-to-text: function [return: [text!] val [<opt> any-value!]] [
        case [
            null? :val ["\null\"]
            object? :val [unspaced ["make object! [" (summarize-obj val) "]"]]
            default [
                trunc: void
                append (
                    mold/limit/truncated :val system/options/dump-size 'trunc
                ) if trunc ["..."]
            ]
        ]
    ]

    dump-one: function [return: <void> item] [
        switch type of item [
            refinement!  ; treat as label, /a no shift and shorter than "a"
            text! [  ; good for longer labeling when you need spaces/etc.
                print unspaced [
                    elide trunc: void
                    mold/limit/truncated item system/options/dump-size 'trunc
                    if trunc ["..."]
                ]
            ]

            word! [
                print [to set-word! item | val-to-text get item]
            ]

            path! [
                print [to set-path! item | val-to-text reduce item]
            ]

            group! [
                print [unspaced [mold item ":"] | val-to-text reeval item]
            ]

            issue! [
                enablements/(prefix): item
            ]

            fail 'value [
                "Item not TEXT!, INTEGER!, WORD!, PATH!, GROUP!:" :item
            ]
        ]
    ]

    case [
        swp: match [set-word! set-path!] :value [ ; `dump x: 1 + 2`
            pos: evaluate/result extra (lit result:)
            set swp :result
            print [swp | result]
        ]

        b: match block! :value [
            while [not tail? b] [
                if swp: match [set-word! set-path!] :b/1 [ ; `dump [x: 1 + 2]`
                    b: evaluate/result b (lit result:)
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
        while [not (new-line? extra) or [tail? extra] or ['| = extra/1]] [
            append/only value extra/1
            all [
                match [block! group!] :extra/1
                contains-newline :extra/1
                break
            ]
            take extra
        ]
        extra: make varargs! []  ; don't allow more takes
    ]
]

dumps: enfixed function [
    {Fast generator for dumping function that uses assigned name for prefix}

    return: [action!]
    :name [set-word!]
    :value "If issue, create non-specialized dumper...#on or #off by default"
        [issue! text! integer! word! set-word! set-path! group! block!]
    extra "Optional variadic data for SET-WORD!, e.g. `dv: dump var: 1 + 2`"
        [<opt> any-value! <variadic>]
][
    if issue? value [
        d: specialize 'dump-to-newline [prefix: as text! name]
        if value <> #off [d #on]  ; note: d hard quotes its argument
    ] else [
        ; Make it easy to declare and dump a variable at the same time.
        ;
        if match [set-word! set-path!] value [
            evaluate/result extra 'value
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
                d'': specialize 'dump [prefix: (as text! name)]
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
summarize-obj: function [
    {Returns a block of information about an object or port}

    return: "Block of short lines (fitting in roughly 80 columns)"
        [<opt> block!]
    obj [object! port!]
    /match "Include only fields that match a string or datatype"
        [text! datatype!]
][
    pattern: match
    match: :lib/match

    form-pad: func [
        {Form a value with fixed size (space padding follows)}
        val
        size
    ][
        val: form val
        insert/dup (tail of val) space (size - length of val)
        val
    ]

    wild: did find (try match text! :pattern) "*"

    return collect-lines [
        for-each [word val] obj [
            if not set? 'val [continue]  ; don't consider unset fields

            type: type of get/any 'val

            str: if match [object!] type [
                spaced [word | words of :val]
            ] else [
                form word
            ]

            switch type of pattern [  ; filter out any non-matching items
                null []

                datatype! [
                    if type != pattern [continue]
                ]

                text! [
                    if wild [
                        fail "Wildcard DUMP-OBJ functionality not implemented"
                    ]
                    if not find str pattern [continue]
                ]

                fail 'pattern
            ]

            if desc: description-of try get/any 'val [
                if 48 < length of desc [
                    desc: append copy/part desc 45 "..."
                ]
            ]

            keep ["  " (form-pad word 15) (form-pad try type 10) :desc]
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
**: enfixed function [
    {Comment until end of line, or end of current BLOCK!/GROUP!}

    return: []
    left "Enfix required for 'fully invisible' enfix behavior (ignored)"
        [<opt> <end> any-value!]
    :args [any-value! <variadic>]
][
    while [(not new-line? args) and [value: take args]] [
        all [
            any-array? :value
            contains-newline value
            return
        ]
    ]
]
