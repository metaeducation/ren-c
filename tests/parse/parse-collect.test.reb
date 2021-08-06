; %parse-collect.test.reb
;
; COLLECT is implemented on top of a generic facility for storing "pending"
; results.  This gives the impression of "rollback"--though what is actually
; happening is that combinators are bubbling up a list of items that can be
; filtered by other combinators to extract data from.
;
; Most combinators allow the bubbling up to happen automatically--where every
; successful parser they call will contribute its results to the growing array
; in the order the parsers were called.  This is not good enough for some
; combinators (like BLOCK!) which have higher-level concepts than the mere
; success of individual parsers to decide what is kept.  (e.g. a parser must
; be part of an entire successful *alternate group* of parsers to have its
; "pendings" contribute to the result).

[(
    x: ~
    did all [
        uparse? [1 2] [x: collect [
            keep integer! keep tag! | keep integer! keep integer!
        ]]
        x = [1 2]
    ]
)(
    x: ~
    did all [  ; semi-nonsensical use of BETWEEN just because it takes 2 rules
        uparse? "(abc)" [x: collect between keep "(" keep ")"]
        x = ["(" ")"]
    ]
)(
    x: <before>
    did all [  ; semi-nonsensical use of BETWEEN just because it takes 2 rules
        not uparse? "(abc}" [x: collect between "(" keep ")"]
        x = <before>
    ]
)(
    x: ~
    did all [
        uparse? "aaa" [x: collect [some [
            keep (try if false [<not kept>])
            keep <any>
            keep (try if true [<kept>])
        ]]]
        x = [#a <kept> #a <kept> #a <kept>]
    ]
)]


; Note potential confusion that SOME KEEP and KEEP SOME are not the same.
[
    (["a" "a" "a"] = uparse "aaa" [collect [some keep "a"]])
    (["a"] = uparse "aaa" [collect [keep some "a"]])
]


; META-BLOCK! can be used to keep the result of a rule as-is, e.g. keeping the
; result of a nested COLLECT as a BLOCK!
[(
    result: uparse "abbbbabbab" [collect [
        some [keep "a", keep [collect [some keep "b" keep (<hi>)]]]
    ]]
    result = ["a" "b" "b" "b" "b" <hi> "a" "b" "b" <hi> "a" "b" <hi>]
)(
    result: uparse "abbbbabbab" [collect [
        some [keep "a", keep ^[collect [some keep "b" keep (<hi>)]]]
    ]]
    result = ["a" ["b" "b" "b" "b" <hi>] "a" ["b" "b" <hi>] "a" ["b" <hi>]]
)]


; You can KEEP inside a KEEP rule.
[
    (["a" "a"] = uparse "aaa" [collect [keep keep "a", "aa"]])
    (["a" "a" "a"] = uparse "aaa" [collect [keep [keep "a" keep "a"] "a"]])
    (["a" "a" "a"] = uparse "aaa" [collect [keep @[keep "a" keep "a"]]])

    (["aaa" "aaa"] = uparse "aaa" [
        collect [keep keep "a", "b" | keep keep "aaa"]
    ])
    (["a" "a" "a" "c" "c"] = uparse "aaa" [
        collect [keep [keep "a" keep "a"] [keep "b" | keep ["a" keep ("c")]]]
    ])
    (null = uparse "aaa" [collect [keep @[keep "a" keep "a" "a"]]])  ; 4 "a"
]


; SOME KEEP vs KEEP SOME
[
    (did all [
        uparse? [1 2 3] [x: collect [keep some integer!]]
        x = [3]
    ])
    (did all [
        uparse? [1 2 3] [x: collect [some keep integer!]]
        x = [1 2 3]
    ])
    (did all [
        uparse? [1 2 3] [x: collect [keep ^[some integer!]]]
        x = [3]
    ])
    (did all [
        uparse? [1 2 3] [x: collect [some [keep ^integer!]]]
        x = [1 2 3]
    ])
]

; Collecting non-array series fragments
[
    (did all [
        pos: uparse* "aaabbb" [x: collect [keep [across some "a"]] <here>]
        "bbb" = pos
        x = ["aaa"]
    ])
    (did all [
        pos: uparse* "aaabbbccc" [
            x: collect [keep [across some "a"] some "b" keep [across some "c"]]
            <here>
        ]
        "" = pos
        x = ["aaa" "ccc"]
    ])
]

; "Backtracking" (more tests needed!)
[
    (did all [
        pos: uparse* [1 2 3] [
            x: collect [
                keep integer! keep integer! keep text!
                |
                keep integer! keep across some integer!
            ]
            <here>
        ]
        [] = pos
        x = [1 2 3]
    ])
]

; No change to variable on failed match (consistent with Rebol2/R3-Alpha/Red
; behaviors w.r.t SET and COPY)
[
    (did all [
        x: <before>
        null = uparse [1 2] [x: collect [keep integer! keep text!]]
        x = <before>
    ])
]

; Nested collect
[
    (did all [
        uparse? [1 2 3 4] [
            a: collect [
                keep integer!
                b: collect [keep across 2 integer!]
                keep integer!
            ]
            <end>
        ]

        a = [1 4]
        b = [2 3]
    ])
]

; GROUP! can be used to keep material that did not originate from the
; input series or a match rule.
[
    (did all [
        pos: uparse* [1 2 3] [
            x: collect [
                keep integer!
                keep (second [A [<pick> <me>] B])
                keep integer!
            ]
            <here>
        ]
        [3] = pos
        x = [1 <pick> <me> 2]
    ])
    (did all [
        pos: uparse* [1 2 3] [
            x: collect [
                keep integer!
                keep ^(second [A [<pick> <me>] B])
                keep integer!
            ]
            <here>
        ]
        [3] = pos
        x = [1 [<pick> <me>] 2]
    ])
    (did all [
        uparse? [1 2 3] [x: collect [keep ^([a b c]) to <end>]]
        x = [[a b c]]
    ])
]

[
    {KEEP without blocks}
    https://github.com/metaeducation/ren-c/issues/935

    (did all [
        uparse? "aaabbb" [x: collect [keep across some "a" keep some "b"]]
        x = ["aaa" "b"]
    ])

    (did all [
        uparse? "aaabbb" [x: collect [keep across to "b"] to <end>]
        x = ["aaa"]
    ])

    (did all [
        uparse? "aaabbb" [
            outer: collect [
                some [inner: collect keep across some "a" | keep some "b"]
            ]
        ]
        outer = ["b"]
        inner = ["aaa"]
    ])
]
