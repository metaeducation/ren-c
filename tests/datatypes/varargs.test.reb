(
    foo: func [x [integer! <variadic>]] [
        sum: 0
        loop [not tail? x] [
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
    f: func ['look [<variadic>]] [try first look]
    blank? applique :f [look: make varargs! []]
)

; !!! Experimental behavior of enfixed variadics, is to act as either 0 or 1
; items.  0 is parallel to <end>, and 1 is parallel to a single parameter.
; It's a little wonky because the evaluation of the parameter happens *before*
; the TAKE is called, but theorized that's still more useful than erroring.
[
    (
        normal: enfixed function [return: [integer!] v [integer! <variadic>]] [
            sum: 0
            loop [not tail? v] [
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
        defers: enfixed function [return: [integer!] v [integer! <variadic>]] [
            sum: 0
            loop [not tail? v] [
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
        soft: enfixed function [:v [any-value! <variadic>]] [
            collect [
                loop [not tail? v] [
                    keep ^(take v)
                ]
            ]
        ]
        true
    )

    ([] = do [soft])
    (
        a: ~void~
        (trap [a soft])/id = 'bad-word-get
    )
    ([7] = do [:(1 + 2) :(3 + 4) soft])
][
    (
        hard: enfixed function [:v [any-value! <variadic>]] [
            collect [
                loop [not tail? v] [
                    keep ^(take v)
                ]
            ]
        ]
        true
    )

    ([] = do [hard])
    (
        a: ~void~
        (trap [a hard])/id = 'bad-word-get
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
        log: adapt :keep [value: reduce value]
        variadic2: func [return: [text!] v [any-value! <variadic>]] [
           log [<1> take v]
           log [<2> take v]
           if not tail? v [fail "THEN SHOULD APPEAR AS IF IT IS VARARGS END"]
           return "returned"
       ]
       result: variadic2 "a" "b" then t -> [log [<t> t] "then"]
       log [<result> result]
    ]

    nblock: collect [
        log: adapt :keep [value: reduce value]
        normal2: func [return: [text!] n1 n2] [
            log [<1> n1 <2> n2]
            return "returned"
        ]
        result: normal2 "a" "b" then t -> [log [<t> t] "then"]
        log [<result> result]
    ]

    vblock == nblock
)
