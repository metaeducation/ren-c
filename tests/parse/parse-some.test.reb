; %parse-some.test.reb
;
; One or more matches.

(
    x: ~
    did all [
        uparse? "aaa" [x: opt some "b", some "a"]
        x = null
    ]
)(
    x: ~
    did all [
        uparse? "aaa" [x: opt some "a"]
        x = "a"
    ]
)


; Unless they are "invisible" (like ELIDE), rules return values.  If the
; rule's purpose is not explicitly to generate new series content (like a
; COLLECT) then it tries to return something very cheap...e.g. a value it
; has on hand, like the rule or the match.  This can actually be useful.
[
    (
        x: null
        did all [
            uparse? "a" [x: "a"]
            "a" = x
        ]
    )(
        x: null
        did all [
            uparse? "aaa" [x: some "a"]
            "a" = x  ; SOME doesn't want to be "expensive" on average
        ]
    )(
        x: null
        did all [
            uparse? "aaa" [x: [some "a" | some "b"]]
            "a" = x  ; demonstrates use of the result (which alternate taken)
        ]
    )
]

[
    (
        res: ~
        did all [
            uparse? [b a a a c] [<any> res: some 'a 'c]
            res = 'a
        ]
    )
    (
        res: ~
        wa: ['a]
        did all [
            uparse? [b a a a c] [<any> res: some wa 'c]
            res = 'a
        ]
    )
]

[
    (uparse? [a a] [some ['a]])
    (not uparse? [a a] [some ['a] 'b])
    (uparse? [a a b a b b b a] [some [<any>]])
    (uparse? [a a b a b b b a] [some ['a | 'b]])
    (not uparse? [a a b a b b b a] [some ['a | 'c]])
    (uparse? [a a b b] [some 'a some 'b])
    (not uparse? [a a b b] [some 'a some 'c])
    (uparse? [b a a a c] [<any> some ['a] 'c])
]

[
    (uparse? "aa" [some [#a]])
    (not uparse? "aa" [some [#a] #b])
    (uparse? "aababbba" [some [<any>]])
    (uparse? "aababbba" [some ["a" | "b"]])
    (not uparse? "aababbba" [some ["a" | #c]])

    (uparse? "aabb" [some #a some "b"])
    (not uparse? "aabb" [some "a" some #c])
]

[https://github.com/red/red/issues/3108
    (uparse? [1] [some further [to <end>]])
    (uparse? [1] [some further [to [<end>]]])
]

(uparse? "baaac" [<any> some [#a] #c])
