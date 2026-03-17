; %prime-factors.test.r
;
; by iArnold

[(
    prime-factors: func [n [integer!]][
        let m: 2
        let s: 1
        let a: copy []
        insist [
            either 0 = n mod m [
                n: n / m
                append a m
            ][
                m: m + s
                s: 2
            ]
            if n < 1.0 * m * m [
                append a n
                n: 1
            ]
            n = 1
        ]
        return a
    ]
    ok
)

    ([1] = prime-factors 1)
    ([2 5] = prime-factors 10)
    ([2 2 3 5 17] = prime-factors 1020)
    ([7 191] = prime-factors 1337)
]
