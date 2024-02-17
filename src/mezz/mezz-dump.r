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

dump: func [
    {Show the name of a value or expressions with the value (See Also: --)}

    return: "Doesn't return anything, not even void (so like a COMMENT)"
        [nihil?]
    :value [any-value?]
    'extra "Optional variadic data for SET-WORD!, e.g. `dump x: 1 + 2`"
        [element? <variadic>]
    /prefix "Put a custom marker at the beginning of each output line"
        [text!]

    <static> enablements (make map! [])
][
    let print: enclose :lib.print lambda [f [frame!]] [
        if prefix [
            if #on <> select enablements prefix [return ~]
            write-stdout prefix
            write-stdout space
        ]
        do f
    ]

    let val-to-text: func [return: [text!] ^val [any-value?]] [
        return case [
            void? val ["; void"]
            quasi? val [unspaced [mold val space space "; anti"]]

            (elide val: unquote val)

            object? val [unspaced ["make object! [" (summarize-obj val) "]"]]
        ] else [
            let trunc
            append (
                [@ trunc]: mold/limit :val system.options.dump-size
            ) if trunc ["..."]
        ]
    ]

    let dump-one: func [return: [~] item] [
        switch/type item [
            refinement?!  ; treat as label, /a no shift and shorter than "a"
            text! [  ; good for longer labeling when you need spaces/etc.
                let trunc
                print unspaced [
                    [@ trunc]: mold/limit item system.options.dump-size
                    if trunc ["..."]
                ]
            ]

            word! [
                print [to set-word! item, val-to-text get/any item]
            ]

            tuple! [
                print [to set-tuple! item, val-to-text reduce item]
            ]

            path! [
                print [to set-path! item, val-to-text reduce item]
            ]

            group! [
                print [unspaced [mold item ":"], val-to-text reeval item]
            ]

            issue! [
                enablements.(prefix): item
            ]

            fail 'value [
                "Item not TEXT!, INTEGER!, WORD!, TUPLE!, PATH!, GROUP!:" :item
            ]
        ]
    ]

    let swp
    case [
        swp: match [set-word! set-path!] :value [  ; `dump x: 1 + 2`
            let pos
            let result: evaluate/next extra $pos
            set swp :result
            print [swp, result]
        ]

        let b: match block! value [
            while [not tail? b] [
                if swp: match [set-word! set-path!] :b.1 [  ; `dump [x: 1 + 2]`
                    result: evaluate/next b $b
                    print [swp, result]
                ] else [
                    dump-one b.1
                    b: next b
                ]
            ]
        ]
    ] else [
        dump-one value
    ]
    return nihil
]

contains-newline: func [return: [logic?] pos [block! group!]] [
    while [pos] [
        any [
            new-line? pos
            all [
                match [block! group!] pos.1
                contains-newline pos.1
            ]
        ] then [return true]

        pos: next pos
    ]
    return false
]

dump-to-newline: adapt :dump [
    if not tail? extra [
        ;
        ; Mutate VARARGS! into a BLOCK!, with passed-in value at the head
        ;
        value: reduce [:value]
        while [all [
            not new-line? extra
            not tail? extra
            ', <> extra.1
        ]] [
            append value take extra
        ]
        extra: make varargs! []  ; don't allow more takes
    ]
]

dumps: enfix func [
    {Fast generator for dumping function that uses assigned name for prefix}

    return: [action?]
    :name [set-word!]
    :value "If issue, create non-specialized dumper...#on or #off by default"
        [issue! text! integer! word! set-word! set-path! group! block!]
    extra "Optional variadic data for SET-WORD!, e.g. `dv: dump var: 1 + 2`"
        [<opt> any-value? <variadic>]
][
    let d
    if issue? value [
        d: specialize :dump-to-newline [prefix: as text! name]
        if value <> #off [d #on]  ; note: d hard quotes its argument
    ] else [
        ; Make it easy to declare and dump a variable at the same time.
        ;
        if match [set-word! set-path!] value [
            value: evaluate extra
            value: either set-word? value [as word! value] [as path! value]
        ]

        ; No way to enable/disable full specializations unless there is
        ; another function or a refinement.  Go with wrapping and adding
        ; refinements for now.
        ;
        ; !!! This actually can't work as invisibles with refinements do not
        ; have a way to be called--in spirit they are like enfix functions,
        ; so SHOVE (>-) would be used, but it doesn't work yet...review.)
        ;
        d: func [return: [nihil?] /on /off <static> d'] compose/deep [
            let d': default [
                let d'': specialize :dump [prefix: (as text! name)]
                d'' #on
            ]
            case [
                on [d' #on]
                off [d' #off]
                /else [d' (value)]
            ]
            return nihil
        ]
    ]
    return set name runs :d
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
summarize-obj: func [
    {Returns a block of information about an object or port}

    return: "Block of short lines (fitting in roughly 80 columns)"
        [<opt> block!]
    obj [object! port! module!]
    /pattern "Include only fields that match a string or datatype"
        [text! type-block!]
][
    let form-pad: lambda [
        {Form a value with fixed size (space padding follows)}
        val
        size
    ][
        let val: form val
        insert/dup (tail of val) space (size - length of val)
        val
    ]

    let wild: to-logic find maybe (match text! maybe pattern) "*"

    return collect [
        for-each [word val] obj [
            if unset? $val [continue]  ; don't consider unset fields

            let kind: kind of noantiform get/any $val

            let str: if kind = object! [
                spaced [word, form words of val]
            ] else [
                form word
            ]

            switch/type pattern [  ; filter out any non-matching items
                null?! []

                type-block! [
                    if kind != pattern [continue]
                ]

                text! [
                    if wild [
                        fail "Wildcard DUMP-OBJ functionality not implemented"
                    ]
                    if not find str pattern [continue]
                ]

                fail 'pattern
            ]

            let desc: description-of noantiform get/any $val
            if desc [
                if 48 < length of desc [
                    desc: append copy/part desc 45 "..."
                ]
            ]

            keep spaced [
                "  " (form-pad word 15) (form-pad kind 10) maybe desc
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
**: func [
    {Comment until end of line, or end of current BLOCK!/GROUP!}

    return: [nihil?]
    'args [element? <variadic>]
][
    let value
    while [all [
        not new-line? args
        value: try take args
    ]] [
        all [
            any-array? value
            contains-newline value
            return nihil
        ]
    ]
    return nihil
]
