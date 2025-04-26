; better than-nothing HIJACK tests

(
    foo: lambda [x] [x + 1]
    another-foo: :foo

    old-foo: copy :foo

    all [
        (old-foo 10) = 11
        hijack 'foo lambda [x] [(old-foo x) + 20]
        (old-foo 10) = 11
        (foo 10) = 31
        (another-foo 10) = 31
    ]
)


; Hijacking and un-hijacking out from under specializations, as well as
; specializing hijacked functions afterward.
(
    three: lambda [x y z /available add-me] [
        x + y + z + either available [add-me] [0]
    ]
    step1: (three 10 20 30) ; 60

    old-three: copy :three

    two-30: specialize 'three [z: 30]
    step2: (two-30 10 20) ; 60

    hijack 'three lambda [a b c /unavailable /available mul-me] [
       a * b * c * either available [mul-me] [1]
    ]

    step3: (three 10 20 30) ; 6000
    step4: (two-30 10 20) ; 6000

    step5: sys/util/rescue [three/unavailable 10 20 30] ; error

    step6: (three/available 10 20 30 40) ; 240000

    step7: (two-30/available 10 20 40) ; 240000

    one-20: specialize 'two-30 [y: 20]

    hijack 'three lambda [q r s] [
        q - r - s
    ]

    step8: (one-20 10) ; -40

    hijack 'three 'old-three

    step9: (three 10 20 30) ; 60

    step10: (two-30 10 20) ; 60

    all [
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
)

; HIJACK of a specialization (needs to notice paramlist has "hidden" params)
(
    two: lambda [a b] [a + b]
    one: specialize 'two [a: 10]
    hijack 'one lambda [b] [20 - b]
    one 20 = 0
)
