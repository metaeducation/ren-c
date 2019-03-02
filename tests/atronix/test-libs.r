REBOL []
recycle/torture
cycle [
    libs: make library! %./libs.so
    N_REPEAT: 10
    read-s10: make routine! compose [
        [
            a [
                struct! [
                    struct! [int32 bi] bs
                    int32 [2] i
                    int32 j
                ]
            ]
            a1 [
                struct! [
                    struct! [int32 bi] bs
                    float  f
                    double d
                ]
            ]
            a2 [
                struct! [
                    struct! [int32 bi] bs
                    int32 [2] i
                    int32 j
                ]
            ]
            a3 [
                struct! [
                    struct! [int32 bi] bs
                    float  f
                    double d
                ]
            ]
            a4 [
                struct! [
                    struct! [int32 bi] bs
                    int32 [2] i
                    int32 j
                ]
            ]
            a5 [
                struct! [
                    struct! [int32 bi] bs
                    int32 [2] i
                    int32 j
                ]
            ]
            a6 [
                struct! [
                    struct! [int32 bi] bs
                    int32 [2] i
                    int32 j
                ]
            ]
            a7 [
                struct! [
                    struct! [int32 bi] bs
                    int32 [2] i
                    int32 j
                ]
            ]
            a8 [
                struct! [
                    struct! [int32 bi] bs
                    int32 [2] i
                    int32 j
                ]
            ]
            a9 [
                struct! [
                    struct! [int32 bi] bs
                    int32 [2] i
                    int32 j
                ]
            ]
        ]
        ((libs)) "read_s10"
    ]

    a: make struct! [
        struct! [int32 bi] bs
        int32 [2] i
        int32 j
    ]
    a1: make struct! [
        struct! [int32 bi] bs
        float f
        double d
    ]
    a2: make struct! [
        struct! [int32 bi] bs
        int32 [2] i
        int32 j
    ]
    a3: make struct! [
        struct! [int32 bi] bs
        float  f
        double d
    ]

    a4: a5: a6: a7: a8: make struct! [
        struct! [int32 bi] bs
        int32 [2] i
        int32 j
    ]

    a9: make struct! [
        struct! [int32 bi] bs
        int32 [2] i
        int32 j
    ]

    i: 0
    while [i < N_REPEAT] [
        a/bs/bi: 100 + i
        a/i/1: 200 + i
        a/i/2: 300 + i
        a/j: 400 + i

        a1/bs/bi: 110 + i
        a1/f: 210 + i
        a1/d: 310 + i

        a2/bs/bi: 120 + i
        a2/i/1: 220 + i
        a2/i/2: 320 + i
        a2/j: 420 + i

        a3/bs/bi: 130 + i
        a3/f: 230 + i
        a3/d: 330 + i

        a9/bs/bi: 190 + i
        a9/i/1: 290 + i
        a9/i/2: 390 + i
        a9/j: 490 + i

        read-s10 a a1 a2 a3 a4 a5 a6 a7 a8 a9
        i: i + 1
    ]

    print ["a9:" mold a9]

    return-s: make routine! compose/deep [
        [
            i [int32]
            return: [(a)]
        ]
        (libs) "return_s"
    ]

    i: 0
    print ["i = " i]
    s: return-s i
    print ["s: " s]
    i: 0
    while [i < N_REPEAT] [
        print ["i = " i]
        s: return-s i
        print ["s:" mold s]
        i: i + 1
    ]
    print now
    wait [2]
]
