; functions/control/apply.r
[
    (did redbol-apply: func [
        return: [<opt> any-value!]
        action [action!]
        block [block!]
        /only
        <local> arg frame params using-args
    ][
        frame: make frame! :action
        params: parameters of :action
        using-args: true

        while [not tail? block] [
            block: if only [
                arg: block.1
                try next block
            ] else [
                try [arg @block]: evaluate block  ; no /VOID, skips invisibles
            ]

            if refinement? params.1 [
                ;
                ; Ren-C allows LOGIC! to control parameterless refinements and
                ; canonizes to either null or #.  Rebol2 allowed any truthy
                ; thing, it does not allow BLANK!.  This makes a BLOCK!-style
                ; apply using positions non-viable.  We OPT all "nones" here.
                ;
                using-args: to-logic set (in frame second params.1) all [get 'arg, #]
            ] else [
                if using-args [  ; v-- should IN allow QUOTED?
                    set (in frame noquote params.1) if meta-word? params.1 [
                        ^ get/any 'arg
                    ] else [
                        get 'arg
                    ]
                ]
            ]

            params: try next params
        ]

        comment [
            ; Too many arguments was not a problem for R3-alpha's APPLY, it
            ; would evaluate them all even if not used by the function.  It
            ; may or may not be better to have it be an error.

            if not tail? block [
                fail "Too many arguments passed in R3-ALPHA-APPLY block."
            ]
        ]

        return do frame  ; nulls are optionals
    ])

    [#44 (
        error? trap [redbol-apply 'append/only [copy [a b] 'c]]
    )]
    (1 == redbol-apply :subtract [2 1])
    (1 = (redbol-apply :- [2 1]))

    ; !!! These were permitted by Rebol2 APPLY, as missing arguments were
    ; treated as #[none].  Ren-C's ~none~ concept is an isotope and cannot be
    ; taken by ordinary function arguments, so this would have to be passing
    ; BLANK! or NULL instead.  It's not clear why support for too few args
    ; would be desirable.
    ;
    ;    (null = redbol-apply lambda [a] [a] [])
    ;    (null = redbol-apply/only lambda [a] [a] [])

    [#2237
        (error? trap [redbol-apply lambda [a] [a] [1 2]])
        (error? trap [redbol-apply/only lambda [a] [a] [1 2]])
    ]

    (error? redbol-apply :make [error! ""])

    (# = redbol-apply lambda [/a] [a] [#[true]])
    (null = redbol-apply lambda [/a] [a] [#[false]])
    (null = redbol-apply lambda [/a] [a] [])
    (# = redbol-apply/only lambda [/a] [a] [#[true]])

    (
        comment {The WORD! false, not #[false], but allowed in Rebol2}

        # = redbol-apply/only lambda [/a] [a] [false]
    )
    (null == redbol-apply/only lambda [/a] [a] [])
    (use [a] [a: true # = redbol-apply lambda [/a] [a] [a]])
    (use [a] [a: false null == redbol-apply lambda [/a] [a] [a]])
    (use [a] [a: false # = redbol-apply lambda [/a] [a] [/a]])
    (use [a] [a: false /a = redbol-apply/only lambda [/a] [/a] [/a]])
    (group! == redbol-apply/only (specialize :of [property: 'type]) [()])
    ([1] == head of redbol-apply :insert [copy [] [1] blank blank])
    ([1] == head of redbol-apply :insert [copy [] [1] blank blank blank false])
    ([[1]] == head of redbol-apply :insert [copy [] [1] blank blank blank true])
    (action! == redbol-apply (specialize :of [property: 'type]) [:print])
    (get-word! == redbol-apply/only (specialize :of [property: 'type]) [:print])

    [
        #1760

        (1 == reeval func [] [redbol-apply does [] [return 1] 2])
        (1 == reeval func [] [redbol-apply func [a] [a] [return 1] 2])
        (1 == reeval func [] [redbol-apply does [] [return 1]])
        (1 == reeval func [] [redbol-apply func [a] [a] [return 1]])
        (1 == reeval func [] [redbol-apply func [a b] [a] [return 1 2]])
        (1 == reeval func [] [redbol-apply func [a b] [a] [2 return 1]])
    ]

    (
        null? redbol-apply lambda [
            x [<opt> any-value!]
        ][
            get 'x
        ][
            null
        ]
    )
    (
        null? redbol-apply lambda [
            'x [<opt> any-value!]
        ][
            get 'x
        ][
            null
        ]
    )
    (
        null? redbol-apply func [
            return: [<opt> any-value!]
            x [<opt> any-value!]
        ][
            return get 'x
        ][
            null
        ]
    )
    (
        bad-word? redbol-apply func [
            return: [<opt> any-value!]
            'x [<opt> any-value!]
        ][
            return get 'x
        ][
            '~none~
        ]
    )
    (
        error? redbol-apply func ['x [<opt> any-value!]] [
            return get 'x
        ][
            make error! ""
        ]
    )
    (
        error? redbol-apply/only func [x [<opt> any-value!]] [
            return get 'x
        ] head of insert copy [] make error! ""
    )
    (
        error? redbol-apply/only func ['x [<opt> any-value!]] [
            return get 'x
        ] head of insert copy [] make error! ""
    )
    (use [x] [x: 1 strict-equal? 1 redbol-apply lambda ['x] [:x] [:x]])
    (use [x] [x: 1 strict-equal? 1 redbol-apply lambda ['x] [:x] [:x]])
    (
        use [x] [
            x: 1
            strict-equal? first [:x] redbol-apply/only lambda [:x] [:x] [:x]
        ]
    )
    (
        use [x] [
            x: ~
            strict-equal? ':x redbol-apply/only func ['x [<opt> any-value!]] [
                return get 'x
            ] [:x]
        ]
    )
    (use [x] [x: 1 strict-equal? 1 redbol-apply lambda [:x] [:x] [x]])
    (use [x] [x: 1 strict-equal? 'x redbol-apply lambda [:x] [:x] ['x]])
    (use [x] [x: 1 strict-equal? 'x redbol-apply/only lambda [:x] [:x] [x]])
    (use [x] [x: 1 strict-equal? 'x redbol-apply/only func [:x] [return :x] [x]])
    (
        use [x] [
            x: ~
            strict-equal? 'x redbol-apply/only func ['x [<opt> any-value!]] [
                return get 'x
            ] [x]
        ]
    )

    [(
        comment {MAKE FRAME! :RETURN should preserve binding in the frame}
        1 == reeval func [] [redbol-apply :return [1] 2]
    )]

    (null == redbol-apply/only lambda [/a] [a] [#[false]])
    (group! == redbol-apply/only :type-of [()])
]
