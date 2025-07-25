; specialize.test.r
;
; Note: GET for partial specialization uses basically the same code
; path as SPECIALIZE does, e.g. these run the same code:
;
;     specialize append:dup:part. []  ; CHAIN! notation, coming soon!
;     get $append:dup:part

[
    (
        foo: func [:A [integer!] :B [integer!] :C [integer!]] [
            return compose [
                :A (reify A) :B (reify B) :C (reify C)
            ]
        ]

        fooBC: get $foo:B:C
        fooCB: get $foo:C:B
        ok
    )

    ([:A ~null~ :B 10 :C 20] = fooBC 10 20)
    ([:A 30 :B 10 :C 20] = fooBC:A 10 20 30)

    ([:A ~null~ :B 20 :C 10] = fooCB 10 20)
    ([:A 30 :B 20 :C 10] = fooCB:A 10 20 30)

    ~bad-parameter~ !! (fooBC:B 1 2 3 4 5 6)
    ~bad-parameter~ !! (fooBC:C 1 2 3 4 5 6)
    ~bad-parameter~ !! (fooCB:B 1 2 3 4 5 6)
    ~bad-parameter~ !! (fooCB:C 1 2 3 4 5 6)
]

(
    /append-123: specialize append/ [value: [1 2 3]]  ; quoted by specialize
    [a b c [1 2 3] [1 2 3]] = append-123:dup copy [a b c] 2
)
(
    /append-123: specialize append/ [value: [1 2 3]]
    /append-123-twice: specialize append-123/ [dup: 2]
    [a b c [1 2 3] [1 2 3]] = append-123-twice copy [a b c]
)
(
    /append-10: specialize append/ [value: 10]
    f: make frame! unrun append-10/
    f.series: copy [a b c]

    comment -[COPY before EVAL allows reuse of F, only the copy is "stolen"]-
    eval copy f
    [a b c 10 10] = eval f
)
(
    f: make frame! append/
    f.series: copy [a b c]
    f.value: [d e f]
    [a b c [d e f]] = eval f
)
(
    foo: func [return: [integer!]] [
        let /return-5: specialize return/ [value: 5]
        return-5
        "this shouldn't be returned"
    ]
    foo = 5
)

[
    (
        /apd: get $append:part:dup
        /apd3: specialize apd/ [dup: 3]
        /ap2d: specialize apd/ [part: 2]

        xy: [<X> #Y]
        abc: [A B C]
        r: [<X> #Y A B A B A B]
        ok
    )

    (r = apd copy xy spread abc 2 3)
    (r = applique apd/ [
        series: copy xy
        set:any $ 'value: spread abc
        part: 2, dup: 3
    ])

    (r = apd3 copy xy spread abc 2)
    (r = applique apd3/ [
        series: copy xy
        set:any $ 'value: spread abc
        part: 2
    ])

    (r = ap2d copy xy spread abc 3)
    (r = applique ap2d/ [
        series: copy xy
        set:any $ 'value: spread abc
        dup: 3
    ])
]

[
    (
        /adp: get $append:dup:part
        /adp2: specialize adp/ [part: 2]
        /ad3p: specialize adp/ [dup: 3]

        xy: [<X> #Y]
        abc: [A B C]
        r: [<X> #Y A B A B A B]
        ok
    )

    (r = adp copy xy spread abc 3 2)
    (r = applique adp/ [
        series: copy xy
        set:any $ 'value: spread abc
        dup: 3, part: 2
    ])

    (r = adp2 copy xy spread abc 3)
    (r = applique adp2/ [
        series: copy xy
        set:any $ 'value: spread abc
        dup: 3
    ])

    (r = ad3p copy xy spread abc 2)
    (r = applique ad3p/ [
        series: copy xy
        set:any $ 'value: spread abc
        part: 2
    ])
]

(
    /aopd3: specialize append/ [
        dup: 3
        part: 1
    ]

    r: [a b c [d e] [d e] [d e]]

    all [
        , r = aopd3 copy [a b c] [d e]
        , r = applique :aopd3 [series: copy [a b c] value: [d e]]
    ]
)

(
    error: null

    for-each 'code [
        [specialize append:asdf/ []]
        [
            /flp: specialize file-to-local:pass/ []
            specialize flp:pass/ []
        ]
    ][
        error: me or (
            'bad-parameter = (sys.util/recover [eval inside [] code]).id
        )
    ]

    not null? error
)


(
    /ap10d: specialize append:dup/ [value: 10]
    f: make frame! ap10d/
    f.series: copy [a b c]
    all [
        [a b c 10] = eval copy f
        f.dup: 2
        [a b c 10 10 10] = eval f
    ]
)

; Making a FRAME! from an ACTION!, and making an ACTION! from a FRAME!
(
    data: [a b c]

    f: copy unrun append/
    f.series: data

    apd: runs f
    apd [d e f]

    data = [a b c [d e f]]
)
