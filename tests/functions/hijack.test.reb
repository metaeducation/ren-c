; better than-nothing HIJACK tests

[
    foo: func [x] [x + 1]
    another-foo: :foo

    old-foo: copy :foo

    did all [
        (old-foo 10) = 11
        hijack 'foo func [x] [(old-foo x) + 20]
        (old-foo 10) = 11
        (foo 10) = 31
        (another-foo 10) = 31
    ]
]


; Hijacking and un-hijacking out from under specializations, as well as
; specializing hijacked functions afterward.
[
    three: func [x y z /available add-me] [
        x + y + z + either available [add-me] [0]
    ]
    step1: (three 10 20 30) ; 60

    old-three: copy :three

    two-30: specialize 'three [z: 30]
    step2: (two-30 10 20) ; 60

    hijack 'three func [a b c /unavailable /available mul-me] [
       a * b * c * either available [mul-me] [1]
    ]

    step3: (three 10 20 30) ; 6000
    step4: (two-30 10 20) ; 6000

    step5: trap [three/unavailable 10 20 30] ; error

    step6: (three/available 10 20 30 40) ; 240000

    step7: (two-30/available 10 20 40) ; 240000

    one-20: specialize 'two-30 [y: 20]

    hijack 'three func [q r s] [
        q - r - s
    ]

    step8: (one-20 10) ; -40

    hijack 'three 'old-three

    step9: (three 10 20 30) ; 60

    step10: (two-30 10 20) ; 60

    did all [
        step1 = 60
        step2 = 60
        step3 = 6000
        step4 = 6000
        error? step5
        step6 = 240000
        step7 = 240000
        step8 = -40
        step9 = 60
        step10 = 60
    ]
]
