[
    foo: func [x [integer! <...>]] [
        sum: 0
        while [not tail? x] [
            sum: sum + take x
        ]
    ]
    y: (z: foo 1 2 3 | 4 5)
    all [y = 5 | z = 6]
]
[
    foo: func [x [integer! <...>]] [make block! x]
    [1 2 3 4] = foo 1 2 3 4
]

;-- !!! Chaining was removed, this test should be rethought or redesigned.
;[
;    alpha: func [x [integer! string! tag! <...>]] [
;        beta 1 2 (x) 3 4
;    ]
;    beta: func ['x [integer! string! word! <...>]] [
;        reverse (make block! x)
;    ]
;    all [
;        [4 3 "back" "wards" 2 1] = alpha "wards" "back"
;            |
;        error? trap [alpha <some> <thing>] ;-- both checks are applied in chain
;            |
;        [4 3 other thing 2 1] = alpha thing other
;    ]
;]

[
    ;-- leaked VARARGS! cannot be accessed after call is over
    error? trap [take eval (foo: func [x [integer! <...>]] [x])]
]

[
    f: func [args [any-value! <opt> <...>]] [
       b: take args
       either tail? args [b] ["not at end"]
    ]
    x: make varargs! [1020]
    1020 = apply :f [args: x]
]

[
    f: func [:look [<...>]][first look]
    blank? apply 'f [look: make varargs! []]
]

; Testing the variadic behavior of |> and <| is easier than rewriting tests
; here to do the same thing.

; <| and |> were originally enfix, so the following tests would have meant x
; would be unset
[
    value: ()
    x: ()

    3 = (value: 1 + 2 <| 30 + 40 x: value  () ())

    did all [value = 3 | x = 3]
][
    value: ()
    x: ()

    70 = (value: 1 + 2 |> 30 + 40 x: value () () ())

    did all [value = 3 | x = 3]
]

[
    void? (<| 10)
][
    void? (10 |>)
]

[
    2 = (1 |> 2 | 3 + 4 | 5 + 6)
][
    1 = (1 <| 2 | 3 + 4 | 5 + 6)
]

