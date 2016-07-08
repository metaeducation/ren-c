; datatypes/action.r

[action? :abs]
[not action? 1]
[function! = type-of :abs]
; bug#1659
; actions are active
[1 == do reduce [:abs -1]]


; DEFAULT tests

[
    unset 'x
    x: default 10
    x = 10
][
    x: _
    x: default 10
    x = 10
][
    x: 20
    x: default 10
    x = 20
][
    o: make object! [x: 10 y: _ z: ()]
    o/x: default 20
    o/y: default 20
    o/z: default 20
    [10 20 20] = reduce [o/x o/y o/z]
]


; better-than-nothing VARARGS! variadic argument tests

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
[
    alpha: func [x [integer! string! tag! <...>]] [
        beta 1 2 (x) 3 4
    ]
    beta: func ['x [integer! string! word! <...>]] [
        reverse (make block! x)
    ]
    all [
        [4 3 "back" "wards" 2 1] = alpha "wards" "back"
            |
        error? trap [alpha <some> <thing>] ;-- both checks are applied in chain
            |
        [4 3 other thing 2 1] = alpha thing other
    ]
]
[
    ;-- leaked VARARGS! cannot be accessed after call is over
    error? trap [take eval (foo: func [x [integer! <...>]] [x])]
]


; better-than-nothing (New)APPLY tests

[
    s: apply :append [series: [a b c] value: [d e] dup: true count: 2]
    s = [a b c d e d e]
]


; better-than-nothing SPECIALIZE tests

[
    append-123: specialize :append [value: [1 2 3] only: true]
    [a b c [1 2 3] [1 2 3]] = append-123/dup [a b c] 2
]
[
    append-123: specialize :append [value: [1 2 3] only: true]
    append-123-twice: specialize :append-123 [dup: true count: 2]
    [a b c [1 2 3] [1 2 3]] = append-123-twice [a b c]
]


; better-than-nothing ADAPT tests

[
    x: 10
    foo: adapt 'any [x: 20]
    foo [1 2 3]
    x = 20
]
[
    capture: blank
    foo: adapt 'any [capture: block]
    all? [
      foo [1 2 3]
      capture = [1 2 3]
    ]
]


; better-than-nothing CHAIN tests

[
    add-one: func [x] [x + 1]
    mp-ad-ad: chain [:multiply | :add-one | :add-one]
    202 = (mp-ad-ad 10 20)
]
[
    add-one: func [x] [x + 1]
    mp-ad-ad: chain [:multiply | :add-one | :add-one]
    sub-one: specialize 'subtract [value2: 1]
    mp-normal: chain [:mp-ad-ad | :sub-one | :sub-one]
    200 = (mp-normal 10 20)
]


; better than-nothing HIJACK tests

[
    foo: func [x] [x + 1]
    another-foo: :foo

    old-foo: hijack 'foo _

    all? [
        (old-foo 10) = 11
        error? try [foo 10] ;-- hijacked function captured but no body
        blank? hijack 'foo func [x] [(old-foo x) + 20]
        (foo 10) = 31
        (another-foo 10) = 31
    ]
]


