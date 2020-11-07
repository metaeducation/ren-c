(
    foo: func [x [integer! <variadic>]] [
        sum: 0
        while [not tail? x] [
            sum: sum + take x
        ]
    ]
    y: (z: foo 1 2 3, 4 5)
    all [y = 5, z = 6]
)
(
    foo: func [x [integer! <variadic>]] [make block! x]
    [1 2 3 4] = foo 1 2 3 4
)

; leaked VARARGS! cannot be accessed after call is over
(
    error? trap [take reeval (foo: func [x [integer! <variadic>]] [x])]
)

(
    f: func [args [any-value! <opt> <variadic>]] [
       b: take args
       either tail? args [b] ["not at end"]
    ]
    x: make varargs! [_]
    blank? applique :f [args: x]
)

(
    f: func [:look [<variadic>]] [to-value first look]
    blank? applique 'f [look: make varargs! []]
)

; !!! Experimental behavior of enfixed variadics, is to act as either 0 or 1
; items.  0 is parallel to <end>, and 1 is parallel to a single parameter.
; It's a little wonky because the evaluation of the parameter happens *before*
; the TAKE is called, but theorized that's still more useful than erroring.
[
    (
        normal: enfixed function [v [integer! <variadic>]] [
            sum: 0
            while [not tail? v] [
                sum: sum + take v
            ]
            return sum + 1
        ]
        true
    )

    (1 = do [normal])
    (11 = do [10 normal])
    (21 = do [10 20 normal])
    (31 = do [x: 30, y: 'x, 1 2 x normal])
    (30 = do [multiply 3 9 normal])  ; seen as ((multiply 3 (9 normal))
][
    (
        defers: enfixed function [v [integer! <variadic>]] [
            sum: 0
            while [not tail? v] [
                sum: sum + take v
            ]
            return sum + 1
        ]
        tweak :defers 'defer on
        true
    )

    (1 = do [defers])
    (11 = do [10 defers])
    (21 = do [10 20 defers])
    (31 = do [x: 30, y: 'x, 1 2 x defers])
    (28 = do [multiply 3 9 defers])  ; seen as (multiply 3 9) defers))
][
    (
        soft: enfixed function ['v [any-value! <variadic>]] [
            collect [
                while [not tail? v] [
                    keep/only take v
                ]
            ]
        ]
        true
    )

    ([] = do [soft])
    (
        a: ~void~
        (trap [a soft])/id = 'need-non-void
    )
    ([7] = do [:(1 + 2) :(3 + 4) soft])
][
    (
        hard: enfixed function [:v [any-value! <variadic>]] [
            collect [
                while [not tail? v] [
                    keep/only take v
                ]
            ]
        ]
        true
    )

    ([] = do [hard])
    (
        a: ~void~
        (trap [a hard])/id = 'need-non-void
    )
    ([(3 + 4)] = do [(1 + 2) (3 + 4) hard])
]


; Testing the variadic behavior of |> and <| is easier than rewriting tests
; here to do the same thing.

; <| and |> were originally enfix, so the following tests would have meant x
; would be unset
(
    unset 'value
    unset 'x

    3 = (value: 1 + 2 <| 30 + 40 x: value  () ())

    did all [value = 3, x = 3]
)
(
    unset 'value

    33 = (value: 1 + 2 |> add 30)

    did all [value = 33]
)

(
    is-barrier?: func [x [<end> integer!]] [null? x]
    is-barrier? (<| 10)
)
(
    10 = (10 |>)
)

(
    1 = (1 <| 2, 3 + 4, 5 + 6)
)

; WATERSHED TEST: This involves the parity of variadics with normal actions,
; showing that simply taking arguments in order gives compatible results.
;
; https://github.com/metaeducation/ren-c/issues/912

(
    vblock: collect [
        log: adapt 'keep [value: reduce value]
        variadic2: func [v [any-value! <variadic>]] [
           log [<1> take v]
           log [<2> take v]
           if not tail? v [fail "THEN SHOULD APPEAR AS IF IT IS VARARGS END"]
           return "returned"
       ]
       result: variadic2 "a" "b" then t -> [log [<t> t] "then"]
       log [<result> result]
    ]

    nblock: collect [
        log: adapt 'keep [value: reduce value]
        normal2: func [n1 n2] [
            log [<1> n1 <2> n2]
            return "returned"
        ]
        result: normal2 "a" "b" then t -> [log [<t> t] "then"]
        log [<result> result]
    ]

    vblock == nblock
)


; Test MAKE FRAME! from a VARARGS! with a test userspace implementation of the
; MATCH operation...
[
    (userspace-match: function [
        {Check value using tests (match types, TRUE or FALSE, or filter)}

        return: "Input if it matched, otherwise null (void if falsey match)"
            [<opt> any-value!]
        :args [<opt> any-value! <variadic>]
        :args-normal [<opt> any-value! <variadic>]
        <local> first-arg
    ][
        test: first args
        switch type of :test [
            word! path! [
                if action? get test [
                    f: make frame! args
                    first-arg: get in f first parameters of action of f
                    either-match false do f [return first-arg]
                    return null
                ]
            ]
        ]

        either-match :(take args) (take args-normal) @null
    ]
    true)

    (userspace-match integer! 10 then [true])
    (userspace-match integer! <tag> else [true])
    (10 = userspace-match even? 10)
    (null = userspace-match even? 7)
]
