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
    x: make varargs! [_]
    blank? apply :f [args: x]
]

[
    f: func [:look [<...>]][first look]
    blank? apply 'f [look: make varargs! []]
]

; !!! Experimental behavior of enfixed variadics, is to act as either 0 or 1
; items.  0 is parallel to <end>, and 1 is parallel to a single parameter.
; It's a little wonky because the evaluation of the parameter happens *before*
; the TAKE is called, but theorized that's still more useful than erroring.
[
    foo: function [v [integer! <...>]] [ ;-- normal parameter
        sum: 0
        while [not tail? v] [
            sum: sum + take v
        ]
        return sum + 1
    ]

    bar: enfix :foo

    all? [
        (bar) = 1
        (10 bar) = 11
        (10 20 bar) = 21
        (x: 30 | y: 'x | 1 2 x bar) = 31
        (multiply 3 9 bar) = 28 ;-- seen as ((multiply 3 9) bar)
    ]
][
    foo: function [#v [integer! <...>]] [ ;-- "tight" parameter
        sum: 0
        while [not tail? v] [
            sum: sum + take v
        ]
        return sum + 1
    ]

    bar: enfix :foo

    all? [
        (bar) = 1
        (10 bar) = 11
        (10 20 bar) = 21
        (x: 30 | y: 'x | 1 2 x bar) = 31
        (multiply 3 9 bar) = 30 ;-- seen as (multiply 3 (9 bar))
    ]
][
    foo: function [:v [any-value! <...>]] [
        stuff: copy []
        while [not tail? v] [
            append/only stuff take v
        ]
        return stuff
    ]

    bar: enfix :foo

    all? [
        (bar) = []
        (a bar) = [a]
        ((1 + 2) (3 + 4) bar) = [(3 + 4)]
    ]
][
    foo: function ['v [any-value! <...>]] [
        stuff: copy []
        while [not tail? v] [
            append/only stuff take v
        ]
        return stuff
    ]

    bar: enfix :foo

    all? [
        (bar) = []
        (a bar) = [a]
        ((1 + 2) (3 + 4) bar) = [7]
    ]
]



; Testing the variadic behavior of |> and <| is easier than rewriting tests
; here to do the same thing.

[
    (value: 1 + 2 <| 30 + 40 () () ())
    value = 3
][
    (value: 1 + 2 |> 30 + 40 () () ())
    value = 70
][
    void? (<| 10)
][
    void? (10 |>)
][
    2 = (1 |> 2 | 3 + 4 | 5 + 6)
][
    1 = (1 <| 2 | 3 + 4 | 5 + 6)
] 
