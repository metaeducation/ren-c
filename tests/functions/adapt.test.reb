; better-than-nothing ADAPT tests

(
    x: 10
    foo: adapt get $any [x: 20]
    foo [1 2 3]
    x = 20
)
(
    capture: blank
    foo: adapt get $any [capture: block]
    all [
      foo [1 2 3]
      capture = [1 2 3]
    ]
)
(
    v: copy []
    append-v: specialize get $append [
        series: v
    ]
    adapted-append-v: adapt get $append-v [
        value: to integer! value
    ]
    adapted-append-v "10"
    adapted-append-v "20"
    v = [10 20]
)

; RETURN is not available at the time of the prelude
[
    (
        captured-x: ~
        foo: func [x] [return "available now"]
        bar: adapt get $foo [
            captured-x: x
            assert [unspecialized? $return]
        ]
        all [
            "available now" = bar 1020
            captured-x = 1020
        ]
    )
]

; A function's locals are not visible to an ADAPT, only the functions on its
; interface.
(
    y: <outside>
    test: func [x <local> y] [return :y]
    adapted: adapt get $test [assert [y = <outside>]]
    nothing? adapted 10
)
