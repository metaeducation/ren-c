; %parse-some.test.r
;
; One or more matches.

(
    x: ~
    all [
        "a" = parse "aaa" [x: try some "b", some "a"]
        x = null
    ]
)(
    x: ~
    all [
        "a" = parse "aaa" [x: try some "a"]
        x = "a"
    ]
)

[#296 (
    n: 0
    <infinite> = catch [
        parse "abc" [
            some [to <end> (n: n + 1, if n = 50 [throw <infinite>])]
        ]
        panic ~#unreachable~
    ]
)(
    n: 0
    all [
        1 = parse "abc" [
            some further [to <end> (n: n + 1)]
        ]
        n = 2
    ]
)]

; Unless they are "invisible" (like ELIDE), rules return values.  If the
; rule's purpose is not explicitly to generate new series content (like a
; COLLECT) then it tries to return something very cheap...e.g. a value it
; has on hand, like the rule or the match.  This can actually be useful.
[
    (
        x: ~
        all [
            "a" = parse "a" [x: "a"]
            "a" = x
        ]
    )(
        x: null
        all [
            "a" = parse "aaa" [x: some "a"]
            "a" = x  ; SOME doesn't want to be "expensive" on average
        ]
    )(
        x: null
        all [
            "a" = parse "aaa" [x: [some "a" | some "b"]]
            "a" = x  ; demonstrates use of the result (which alternate taken)
        ]
    )
]

[
    (
        res: ~
        all [
            'c = parse [b a a a c] [<next> res: some 'a 'c]
            res = 'a
        ]
    )
    (
        res: ~
        wa: ['a]
        all [
            'c = parse [b a a a c] [<next> res: some wa 'c]
            res = 'a
        ]
    )
]

[
    ('a = parse [a a] [some ['a]])

    ~parse-mismatch~ !! (parse [a a] [some ['a] 'b])

    ('a = parse [a a b a b b b a] [some [one]])
    ('a = parse [a a b a b b b a] [some ['a | 'b]])

    ~parse-incomplete~ !! (parse [a a b a b b b a] [some ['a | 'c]])

    ('b = parse [a a b b] [some 'a some 'b])

    ~parse-mismatch~ !! (parse [a a b b] [some 'a some 'c])

    ('c = parse [b a a a c] [<next> some ['a] 'c])
]

[
    (#a = parse "aa" [some [#a]])

    ~parse-mismatch~ !! (parse "aa" [some [#a] #b])

    (#a = parse "aababbba" [some [one]])
    ("a" = parse "aababbba" [some ["a" | "b"]])

    ~parse-incomplete~ !! (parse "aababbba" [some ["a" | #c]])

    ("b" = parse "aabb" [some #a some "b"])

    ~parse-mismatch~ !! (parse "aabb" [some "a" some #c])
]

[https://github.com/red/red/issues/3108
    (void? parse [1] [some further [to <end>]])
    (void? parse [1] [some further [to [<end>]]])
]

(#c = parse "baaac" [one some [#a] #c])


; OPT SOME tests (which used to be WHILE)

(
    x: ~
    all [
        "a" = parse "aaa" [x: try some "b", opt some "a"]
        null? x
    ]
)

[
    (^void = parse [] [opt some 'a])
    (^void = parse [] [opt some 'b])
    ('a = parse [a] [opt some 'a])

    ~parse-incomplete~ !! (parse [a] [opt some 'b])

    ('a = parse [a] [opt some 'b one])
    ('b = parse [a b a b] [opt some ['b | 'a]])
]

[(
    x: ~
    all [
        "a" = parse "aaa" [x: try some "a"]
        x = "a"
    ]
)]

[
    (^void = parse "a" ["a" opt some "b"])
    (^void = parse "a" ["a" [opt "b"]])
    ('~[]~ = parse "a" ["a" [/lift opt some "b"]])
]

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    j: ~
    try parse "a" [opt some [
        (
            i: i + 1
            j: if i = 2 [[<end> <next>]]
        )
        j
    ]]
    i = 2
)

[#1268 (
    i: 0
    <infinite?> = catch [
        parse "a" [opt some [(i: i + 1, if i > 100 [throw <infinite?>])]]
    ]
)(
    i: 0
    j: ~
    all [
        error? parse "a" [opt some [(i: i + 1, j: if i = 2 '[veto]) j]]
        i = 2
    ]
)]


[
    (^void = parse "" [opt some #a])
    (^void = parse "" [opt some #b])
    (#a = parse "a" [opt some #a])
    ~parse-incomplete~ !! (parse "a" [opt some #b])
    (#a = parse "a" [opt some #b one])
    (#b = parse "abab" [opt some [#b | #a]])
]

; WHILE tests from %parse-test.red, rethought as OPT SOME
[
    (
        x: ~
        ok
    )
    (#{06} = parse #{020406} [
        opt some [x: across one elide cond (even? first x)]
    ])

    ~parse-mismatch~ !! (
        parse #{01} [x: across one elide cond (even? first x)]
    )
    ~parse-mismatch~ !! (
        parse #{0105} [some [x: across one elide cond (even? first x)]]
    )

    (^void = parse #{} [opt some #{0A}])
    (^void = parse #{} [opt some #{0B}])
    (#{0A} = parse #{0A} [opt some #{0A}])

    ~parse-incomplete~ !! (parse #{0A} [opt some #{0B}])

    (10 = parse #{0A} [opt some #{0B} one])
    (#{0B} = parse #{0A0B0A0B} [opt some [#{0B} | #{0A}]])

    ~parse-mismatch~ !! (parse #{0A} [opt some #{0A} #{0A}])

    (1 = parse #{01} [ahead [#{0A} | #"^A"] one])
]

[
    ('a = parse [a a] [opt some 'a])
    (^void = parse [a a] [opt some 'a, opt some 'b])
]
