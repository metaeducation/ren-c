; %parse-into.test.reb
;
; SUPARSE is an arity-2 of historical Rebol PARSE INTO, permitting use of a
; value-bearing rule to produce the thing to recurse the parser into...which
; can generate a new series, as well as pick one out of a block.

[
    ('~()~ = ^ parse [[]] [subparse any-series! []])
    ('a == parse [[a]] [subparse any-series! ['a]])
    ('c == parse [b [a] c] ['b subparse any-series! ['a] 'c])
    (#a == parse ["a"] [subparse any-series! [#a]])
    ('c == parse [b "a" c] ['b subparse any-series! ["a"] 'c])
    (#a == parse [["a"]] [subparse block! [subparse any-series! [#a]]])
    (didn't parse [[a]] [subparse any-series! ['a 'b]])
    (didn't parse [[a]] [subparse any-series! [some 'b]])
    ([a] == parse [[a]] [subparse any-series! ['a 'b] | block!])
]

("a" == parse ["aa"] [subparse text! ["a" "a"]])

; One key feature of UPARSE is that rule chaining is done in such a way that
; it delegates the recognition to the parse engine, meaning that rules do not
; have to be put into blocks as often.
[
    ("a" = parse ["aaaa"] [subparse text! some repeat 2 "a"])
    (null = parse ["aaaa"] [subparse text! some repeat 3 "a"])
]


(
    did all [
        "aaa" == parse ["aaa"] [subparse text! [x: across some "a"]]
        x = "aaa"
    ]
)

(
    did all [
        "aaa" == parse ["aaa"] [subparse <any> [x: across some "a"]]
        x = "aaa"
    ]
)

(
    did all [
        "aaa" == parse "((aaa)))" [
            subparse [between some "(" some ")"] [x: across some "a"]
        ]
        x = "aaa"
    ]
)

(
    did all [
        [some some some] == parse [| | some some some | | |] [
            content: between some '| some '|
            subparse (content) [x: collect [some keep ['some]]]
        ]
        x = [some some some]
    ]
)

[(
    "" == parse "baaabccc" [
        subparse [between "b" "b"] [some "a" <end>] to <end>
    ]
)(
    didn't parse "baaabccc" [
        subparse [between "b" "b"] ["a" <end>], to <end>
    ]
)(
    didn't parse "baaabccc" [subparse [between "b" "b"] ["a"], to <end>]
)(
    "" == parse "baaabccc" [
        subparse [between "b" "b"] ["a" to <end>], "c", to <end>
    ]
)(
    "" == parse "aaabccc" [subparse [across to "b"] [some "a"], to <end>]
)]


; SUBPARSE can be mixed with HERE to parse into the same series
;
; Note: If functions with INPUT that return progress would act implicitly as
; combinators, then SUBPARSE <HERE> would be how PARSE would act.
[(
    x: match-parse "aaabbb" [
        some "a"
        subparse <here> ["bbb" (b: "yep, Bs")]
        "bbb" (bb: "Bs again")
    ]
    did all [
        x = "aaabbb"
        b = "yep, Bs"
        bb = "Bs again"
    ]
)(
    x: match-parse "aaabbbccc" [
        some "a"
        subparse <here> ["bbb" to <end> (b: "yep, Bs")]
        "bbb" (bb: "Bs again")
        "ccc" (c: "Here be Cs")
    ]
    did all [
        x = "aaabbbccc"
        b = "yep, Bs"
        bb = "Bs again"
        c = "Here be Cs"
    ]
)]

; SUBPARSE is not legal if a string parse is already running
;
(["a" "a"] = parse "aa" [collect [subparse <here> [some keep "a"]] elide "aa"])

; Manual SUBPARSE via a recursion
[
    ("test" = parse [a "test"] [
        'a s: text! (assert [#t == parse s [repeat 4 <any>]])
    ])
]

; Parsing ANY-SEQUENCE is allowed
(
    'c = parse [a//c] [subparse path! ['a _ 'c]]
)
