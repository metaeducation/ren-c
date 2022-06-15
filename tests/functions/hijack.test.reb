; better than-nothing HIJACK tests

(
    foo: func [x] [return x + 1]
    another-foo: :foo

    old-foo: copy :foo

    did all [
        (old-foo 10) = 11
        hijack :foo func [x] [return (old-foo x) + 20]
        (old-foo 10) = 11
        (foo 10) = 31
        (another-foo 10) = 31
    ]
)


; Hijacking and un-hijacking out from under specializations, as well as
; specializing hijacked functions afterward.
[
    (
        three: func [x y z /available "add me" [integer!]] [
            return x + y + z + either available [available] [0]
        ]
        60 = (three 10 20 30)
    )

    (
        old-three: copy :three

        two-30: specialize :three [z: 30]
        60 = (two-30 10 20)
    )

    (
        hijack :three func [
            a b c /unavailable /available "mul me" [integer!]
        ][
            return a * b * c * either available [available] [1]
        ]
        true
    )

    (6000 = (three 10 20 30))
    (6000 = (two-30 10 20))

    (error? trap [three/unavailable 10 20 30])

    (240000 = (three/available 10 20 30 40))

    (240000 = (two-30/available 10 20 40))

    (
        one-20: specialize :two-30 [y: 20]

        hijack :three func [q r s] [
            return q - r - s
        ]

        true
    )

    (-40 = (one-20 10))

    (
        hijack :three :old-three
        true
    )

    (60 = (three 10 20 30))

    (60 = (two-30 10 20))
]

; HIJACK of a specialization (needs to notice paramlist has "hidden" params)
(
    two: func [a b] [a + b]
    one: specialize :two [a: 10]
    hijack :one lambda [b] [20 - b]
    0 = one 20
)
