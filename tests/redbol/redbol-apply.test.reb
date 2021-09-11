; functions/control/apply.r

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

    loop [not tail? block] [
        block: if only [
            arg: get* 'block/1
            try next block
        ] else [
            ; Skip comments and other invisibles.
            ;
            until [not quoted? [block arg]: evaluate block]
            try block
        ]

        if refinement? params/1 [
            comment {
                Ren-C allows LOGIC! to control parameterless refinements and
                canonizes to either null or the refinement name itself.  But
                it does not allow BLANK!.  This makes a BLOCK!-style apply
                using positions non-viable.  We OPT all nones here.
            }
            using-args: did set (in frame second params/1) opt get* 'arg
        ] else [
            if using-args [
                set (in frame params/1) get* 'arg
            ]
        ]

        params: try next params
    ]

    comment [
        {Too many arguments was not a problem for R3-alpha's APPLY, it
        would evaluate them all even if not used by the function.  It
        may or may not be better to have it be an error.}

        if not tail? block [
            fail "Too many arguments passed in R3-ALPHA-APPLY block."
        ]
    ]

    do frame  comment {nulls are optionals}
])

[#44 (
    error? trap [redbol-apply 'append/only [copy [a b] 'c]]
)]
(1 == redbol-apply :subtract [2 1])
(1 = (redbol-apply :- [2 1]))
(null = redbol-apply func [a] [a] [])
(null = redbol-apply/only func [a] [a] [])

[#2237
    (error? trap [redbol-apply func [a] [a] [1 2]])
    (error? trap [redbol-apply/only func [a] [a] [1 2]])
]

(error? redbol-apply :make [error! ""])

(# = redbol-apply func [/a] [a] [#[true]])
(null = redbol-apply func [/a] [a] [#[false]])
(null = redbol-apply func [/a] [a] [])
(# = redbol-apply/only func [/a] [a] [#[true]])

(
    comment {The WORD! false, not #[false], but allowed in Rebol2}

    # = redbol-apply/only func [/a] [a] [false]
)
(null == redbol-apply/only func [/a] [a] [])
(use [a] [a: true # = redbol-apply func [/a] [a] [a]])
(use [a] [a: false null == redbol-apply func [/a] [a] [a]])
(use [a] [a: false # = redbol-apply func [/a] [a] [/a]])
(use [a] [a: false # = redbol-apply/only func [/a] [/a] [/a]])
(group! == redbol-apply/only (specialize :of [property: 'type]) [()])
([1] == head of redbol-apply :insert [copy [] [1] blank blank])
([1] == head of redbol-apply :insert [copy [] [1] blank false])
([[1]] == head of redbol-apply :insert [copy [] [1] blank true])
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
    null? redbol-apply func [
        return: [<opt> any-value!]
        x [<opt> any-value!]
    ][
        get 'x
    ][
        null
    ]
)
(
    null? redbol-apply func [
        return: [<opt> any-value!]
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
        return get/any 'x
    ][
        ~void~
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
(use [x] [x: 1 strict-equal? 1 redbol-apply func ['x] [:x] [:x]])
(use [x] [x: 1 strict-equal? 1 redbol-apply func ['x] [:x] [:x]])
(
    use [x] [
        x: 1
        strict-equal? first [:x] redbol-apply/only func [:x] [:x] [:x]
    ]
)
(
    use [x] [
        unset 'x
        strict-equal? first [:x] redbol-apply/only func ['x [<opt> any-value!]] [
            return get 'x
        ] [:x]
    ]
)
(use [x] [x: 1 strict-equal? 1 redbol-apply func [:x] [:x] [x]])
(use [x] [x: 1 strict-equal? 'x redbol-apply func [:x] [:x] ['x]])
(use [x] [x: 1 strict-equal? 'x redbol-apply/only func [:x] [:x] [x]])
(use [x] [x: 1 strict-equal? 'x redbol-apply/only func [:x] [return :x] [x]])
(
    use [x] [
        unset 'x
        strict-equal? 'x redbol-apply/only func ['x [<opt> any-value!]] [
            return get 'x
        ] [x]
    ]
)

[(
    comment {MAKE FRAME! :RETURN should preserve binding in the frame}
    1 == reeval func [] [redbol-apply :return [1] 2]
)]

(null == redbol-apply/only func [/a] [a] [#[false]])
(group! == redbol-apply/only :type-of [()])
