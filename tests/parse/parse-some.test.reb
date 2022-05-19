; %parse-some.test.reb
;
; One or more matches.

(
    x: ~
    did all [
        "a" == uparse "aaa" [x: opt some "b", some "a"]
        x = null
    ]
)(
    x: ~
    did all [
        "a" == uparse "aaa" [x: opt some "a"]
        x = "a"
    ]
)

[#296 (
    n: 0
    <infinite> = catch [
        uparse "abc" [
            some [to <end> (n: n + 1, if n = 50 [throw <infinite>])]
        ]
        fail ~unreachable~
    ]
)(
    n: 0
    did all [
        1 == uparse "abc" [
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
            "a" == uparse "a" [x: "a"]
            "a" = x
        ]
    )(
        x: null
        did all [
            "a" == uparse "aaa" [x: some "a"]
            "a" = x  ; SOME doesn't want to be "expensive" on average
        ]
    )(
        x: null
        did all [
            "a" == uparse "aaa" [x: [some "a" | some "b"]]
            "a" = x  ; demonstrates use of the result (which alternate taken)
        ]
    )
]

[
    (
        res: ~
        did all [
            'c == uparse [b a a a c] [<any> res: some 'a 'c]
            res = 'a
        ]
    )
    (
        res: ~
        wa: ['a]
        did all [
            'c == uparse [b a a a c] [<any> res: some wa 'c]
            res = 'a
        ]
    )
]

[
    ('a == uparse [a a] [some ['a]])
    (didn't uparse [a a] [some ['a] 'b])
    ('a == uparse [a a b a b b b a] [some [<any>]])
    ('a == uparse [a a b a b b b a] [some ['a | 'b]])
    (didn't uparse [a a b a b b b a] [some ['a | 'c]])
    ('b == uparse [a a b b] [some 'a some 'b])
    (didn't uparse [a a b b] [some 'a some 'c])
    ('c == uparse [b a a a c] [<any> some ['a] 'c])
]

[
    (#a == uparse "aa" [some [#a]])
    (didn't uparse "aa" [some [#a] #b])
    (#a == uparse "aababbba" [some [<any>]])
    ("a" == uparse "aababbba" [some ["a" | "b"]])
    (didn't uparse "aababbba" [some ["a" | #c]])

    ("b" == uparse "aabb" [some #a some "b"])
    (didn't uparse "aabb" [some "a" some #c])
]

[https://github.com/red/red/issues/3108
    ([] == uparse [1] [some further [to <end>]])
    ([] == uparse [1] [some further [to [<end>]]])
]

(#c == uparse "baaac" [<any> some [#a] #c])


; OPT SOME or MAYBE SOME tests (which used to be WHILE)

(
    x: ~
    did all [
        "a" == uparse "aaa" [x: maybe some "b", opt some "a"]
        unset? 'x
    ]
)

[
    (none? uparse [] [maybe some 'a])
    (none? uparse [] [maybe some 'b])
    ('a == uparse [a] [maybe some 'a])
    (didn't uparse [a] [maybe some 'b])
    ('a == uparse [a] [maybe some 'b <any>])
    ('b == uparse [a b a b] [maybe some ['b | 'a]])
]

[(
    x: ~
    did all [
        "a" == uparse "aaa" [x: maybe some "a"]
        x = "a"
    ]
)]

; OPT SOME that never actually has a succeeding rule gives back a match that
; is a ~null~ isotope, which decays to null
[
    ('~null~ = ^ uparse "a" ["a" opt some "b"])
    ('~null~ = ^ uparse "a" ["a" [opt "b"]])
    ('~void~ = uparse "a" ["a" ^[maybe some "b"]])
]

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    uparse "a" [maybe some [(i: i + 1 j: if i = 2 [[<end> skip]]) j]]
    i == 2
)

[#1268 (
    i: 0
    <infinite?> = catch [
        uparse "a" [maybe some [(i: i + 1) (if i > 100 [throw <infinite?>])]]
    ]
)(
    i: 0
    uparse "a" [maybe some [(i: i + 1 j: try if i = 2 [[false]]) j]]
    i == 2
)]


[
    (none? uparse "" [maybe some #a])
    (none? uparse "" [maybe some #b])
    (#a == uparse "a" [maybe some #a])
    (didn't uparse "a" [maybe some #b])
    (#a == uparse "a" [maybe some #b <any>])
    (#b == uparse "abab" [maybe some [#b | #a]])
]

; WHILE tests from %parse-test.red, rethought as MAYBE SOME or OPT SOME
[
    (
        x: blank
        true
    )
    (#[true] == uparse #{020406} [
        maybe some [x: across <any> :(even? first x)]
    ])
    (didn't uparse #{01} [x: across <any> :(even? first x)])
    (didn't uparse #{0105} [some [x: across <any> :(even? first x)]])
    (none? uparse #{} [maybe some #{0A}])
    (none? uparse #{} [maybe some #{0B}])
    (#{0A} == uparse #{0A} [maybe some #{0A}])
    (didn't uparse #{0A} [maybe some #{0B}])
    (10 == uparse #{0A} [maybe some #{0B} <any>])
    (#{0B} == uparse #{0A0B0A0B} [maybe some [#{0B} | #{0A}]])
    (error? trap [uparse #{} [ahead]])
    (didn't uparse #{0A} [maybe some #{0A} #{0A}])
    (1 == uparse #{01} [ahead [#{0A} | #"^A"] <any>])
]

[
    ('a == uparse [a a] [maybe some 'a])
    ('a == uparse [a a] [maybe some 'a, maybe some 'b])
]
