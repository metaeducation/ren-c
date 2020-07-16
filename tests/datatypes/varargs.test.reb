(
    foo: func [x [integer! <...>]] [
        sum: 0
        while-not [tail? x] [
            sum: sum + take x
        ]
    ]
    y: (z: foo 1 2 3 | 4 5)
    all [y = 5 | z = 6]
)
(
    foo: func [x [integer! <...>]] [make block! x]
    [1 2 3 4] = foo 1 2 3 4
)

(
    ;-- leaked VARARGS! cannot be accessed after call is over
    error? trap [take reeval (foo: func [x [integer! <...>]] [x])]
)

(
    f: func [args [any-value! <opt> <...>]] [
       b: take args
       either tail? args [b] ["not at end"]
    ]
    x: make varargs! [_]
    blank? applique :f [args: x]
)

(
    f: func [:look [<...>]] [to-value first look]
    blank? applique 'f [look: make varargs! []]
)

; !!! Experimental behavior of enfixed variadics, is to act as either 0 or 1
; items.  0 is parallel to <end>, and 1 is parallel to a single parameter.
; It's a little wonky because the evaluation of the parameter happens *before*
; the TAKE is called, but theorized that's still more useful than erroring.
[
    (
        normal: enfix function [v [integer! <...>]] [
            sum: 0
            while [not tail? v] [
                sum: sum + take v
            ]
            return sum
        ]
        true
    )

    (do [normal] = 0)
    (do [10 normal] = 10)
    (do [10 20 normal] = 20)
    (do [x: 30 | y: 'x | 1 2 x normal] = 30)
    (do [multiply 3 9 normal] = 27) ;-- seen as ((multiply 3 9) normal)
][
    (
        tight: enfix function [#v [integer! <...>]] [
            sum: 0
            while [not tail? v] [
                sum: sum + take v
            ]
            return sum
        ]
        true
    )

    (do [tight] = 0)
    (do [10 tight] = 10)
    (do [10 20 tight] = 20)
    (do [x: 30 | y: 'x | 1 2 x tight] = 30)
    (do [multiply 3 9 tight] = 27) ;-- seen as (multiply 3 (9 tight))
][
    (
        soft: enfix function ['v [any-value! <...>]] [
            stuff: copy []
            while [not tail? v] [
                append/only stuff take v
            ]
            return stuff
        ]
        true
    )

    (do [soft] = [])
    (do [a soft] = [a])
    (do [(1 + 2) (3 + 4) soft] = [7])
][
    (
        hard: enfix function [:v [any-value! <...>]] [
            stuff: copy []
            while [not tail? v] [
                append/only stuff take v
            ]
            return stuff
        ]
        true
    )

    (do [hard] = [])
    (do [a hard] = [a])
    (do [(1 + 2) (3 + 4) hard] = [(3 + 4)])
]


; Testing the variadic behavior of |> and <| is easier than rewriting tests
; here to do the same thing.

; <| and |> were originally enfix, so the following tests would have meant x
; would be void
(
    value: void
    x: void

    3 = (value: 1 + 2 <| 30 + 40 x: value  () ())

    did all [value = 3 | x = 3]
)
(
    value: void
    x: void

    70 = (value: 1 + 2 |> 30 + 40 x: value () () ())

    did all [value = 3 | x = 3]
)

(
    is-barrier?: func [x [<end> integer!]] [unset? 'x]
    is-barrier? (<| 10)
)
(
    void? (10 |>)
)

(
    2 = (1 |> 2 | 3 + 4 | 5 + 6)
)
(
    1 = (1 <| 2 | 3 + 4 | 5 + 6)
)

