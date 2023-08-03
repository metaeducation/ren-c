; %parse-some.test.reb
;
; One or more matches.

(
    x: ~
    did all [
        "a" == parse "aaa" [x: try some "b", some "a"]
        x = null
    ]
)(
    x: ~
    did all [
        "a" == parse "aaa" [x: try some "a"]
        x = "a"
    ]
)

[#296 (
    n: 0
    <infinite> = catch [
        parse "abc" [
            some [to <end> (n: n + 1, if n = 50 [throw <infinite>])]
        ]
        fail ~unreachable~
    ]
)(
    n: 0
    did all [
        1 == parse "abc" [
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
        did all [
            "a" == parse "a" [x: "a"]
            "a" = x
        ]
    )(
        x: null
        did all [
            "a" == parse "aaa" [x: some "a"]
            "a" = x  ; SOME doesn't want to be "expensive" on average
        ]
    )(
        x: null
        did all [
            "a" == parse "aaa" [x: [some "a" | some "b"]]
            "a" = x  ; demonstrates use of the result (which alternate taken)
        ]
    )
]

[
    (
        res: ~
        did all [
            'c == parse [b a a a c] [<any> res: some 'a 'c]
            res = 'a
        ]
    )
    (
        res: ~
        wa: ['a]
        did all [
            'c == parse [b a a a c] [<any> res: some wa 'c]
            res = 'a
        ]
    )
]

[
    ('a == parse [a a] [some ['a]])
    (didn't parse [a a] [some ['a] 'b])
    ('a == parse [a a b a b b b a] [some [<any>]])
    ('a == parse [a a b a b b b a] [some ['a | 'b]])
    (didn't parse [a a b a b b b a] [some ['a | 'c]])
    ('b == parse [a a b b] [some 'a some 'b])
    (didn't parse [a a b b] [some 'a some 'c])
    ('c == parse [b a a a c] [<any> some ['a] 'c])
]

[
    (#a == parse "aa" [some [#a]])
    (didn't parse "aa" [some [#a] #b])
    (#a == parse "aababbba" [some [<any>]])
    ("a" == parse "aababbba" [some ["a" | "b"]])
    (didn't parse "aababbba" [some ["a" | #c]])

    ("b" == parse "aabb" [some #a some "b"])
    (didn't parse "aabb" [some "a" some #c])
]

[https://github.com/red/red/issues/3108
    ([] == parse [1] [some further [to <end>]])
    ([] == parse [1] [some further [to [<end>]]])
]

(#c == parse "baaac" [<any> some [#a] #c])


; TRY SOME or MAYBE SOME tests (which used to be WHILE)

(
    x: ~
    did all [
        "a" == parse "aaa" [x: try some "b", try some "a"]
        null? x
    ]
)

[
    ('~[~null~]~ = ^ parse [] [try some 'a])
    ('~[~null~]~ = ^(parse [] [try some 'b]))
    ('a == parse [a] [try some 'a])
    (didn't parse [a] [try some 'b])
    ('a == parse [a] [try some 'b <any>])
    ('b == parse [a b a b] [try some ['b | 'a]])
]

[(
    x: ~
    did all [
        "a" == parse "aaa" [x: try some "a"]
        x = "a"
    ]
)]

[
    ('~[~null~]~ = ^ parse "a" ["a" try some "b"])
    ('~[~null~]~ = ^ parse "a" ["a" [try "b"]])
    ('~null~ = parse "a" ["a" ^[try some "b"]])
]

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    parse "a" [try some [
        (
            i: i + 1
            j: if i = 2 [[<end> <any>]]
        )
        j
    ]]
    i == 2
)

[#1268 (
    i: 0
    <infinite?> = catch [
        parse "a" [try some [(i: i + 1, if i > 100 [throw <infinite?>])]]
    ]
)(
    i: 0
    did all [
        didn't parse "a" [try some [(i: i + 1, j: if i = 2 [[false]]) j]]
        i == 2
    ]
)]


[
    ('~[~null~]~ = ^ parse "" [try some #a])
    ('~[~null~]~ = ^ parse "" [try some #b])
    (#a == parse "a" [try some #a])
    (didn't parse "a" [try some #b])
    (#a == parse "a" [try some #b <any>])
    (#b == parse "abab" [try some [#b | #a]])
]

; WHILE tests from %parse-test.red, rethought as TRY SOME
[
    (
        x: blank
        true
    )
    (#{06} == parse #{020406} [
        try some [x: across <any> :(even? first x)]
    ])
    (didn't parse #{01} [x: across <any> :(even? first x)])
    (didn't parse #{0105} [some [x: across <any> :(even? first x)]])
    ('~[~null~]~ = ^ parse #{} [try some #{0A}])
    ('~[~null~]~ = ^ parse #{} [try some #{0B}])
    (#{0A} == parse #{0A} [try some #{0A}])
    (didn't parse #{0A} [try some #{0B}])
    (10 == parse #{0A} [try some #{0B} <any>])
    (#{0B} == parse #{0A0B0A0B} [try some [#{0B} | #{0A}]])

    ~???~ !! (parse #{} [ahead])

    (didn't parse #{0A} [try some #{0A} #{0A}])
    (1 == parse #{01} [ahead [#{0A} | #"^A"] <any>])
]

[
    ('a == parse [a a] [try some 'a])
    ('~[~null~]~ == ^ parse [a a] [try some 'a, try some 'b])
]
