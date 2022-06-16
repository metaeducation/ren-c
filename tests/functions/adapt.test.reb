; better-than-nothing ADAPT tests

(
    x: 10
    foo: adapt :any [x: 20]
    foo [1 2 3]
    x = 20
)
(
    capture: blank
    foo: adapt :any [capture: block]
    did all [
      foo [1 2 3]
      capture = [1 2 3]
    ]
)
(
    v: copy []
    append-v: specialize :append [
        series: v
    ]
    adapted-append-v: adapt :append-v [
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
        bar: adapt :foo [
            captured-x: x
            assert [unset? 'return]
        ]
        did all [
            "available now" = bar 1020
            captured-x = 1020
        ]
    )
]
