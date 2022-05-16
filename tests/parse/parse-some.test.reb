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
