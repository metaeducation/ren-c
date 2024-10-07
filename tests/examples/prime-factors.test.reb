; %prime-factors.test.reb
;
; by iArnold

[(
    /prime-factors: func [n [integer!]][
        let m: 2
        let s: 1
        let a: copy []
        until [
            either n mod m = 0 [
                n: n / m
                append a m
            ][
                m: m + s
                s: 2
            ]
            if 1.0 * m * m > n [
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
