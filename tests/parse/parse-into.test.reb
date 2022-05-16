; %parse-into.test.reb
;
; UPARSE INTO is arity-2, permitting use of a value-bearing rule to produce
; the thing to recurse the parser into...which can generate a new series, as
; well as pick one out of a block.

[
    (none? uparse [[]] [into any-series! []])
    ('a == uparse [[a]] [into any-series! ['a]])
    ('c == uparse [b [a] c] ['b into any-series! ['a] 'c])
    (#a == uparse ["a"] [into any-series! [#a]])
    ('c == uparse [b "a" c] ['b into any-series! ["a"] 'c])
    (#a == uparse [["a"]] [into block! [into any-series! [#a]]])
    (didn't uparse [[a]] [into any-series! ['a 'b]])
    (didn't uparse [[a]] [into any-series! [some 'b]])
    ([a] == uparse [[a]] [into any-series! ['a 'b] | block!])
]

("a" == uparse ["aa"] [into text! ["a" "a"]])

; One key feature of UPARSE is that rule chaining is done in such a way that
; it delegates the recognition to the parse engine, meaning that rules do not
; have to be put into blocks as often.
[
    ("a" = uparse ["aaaa"] [into text! some 2 "a"])
    (null = uparse ["aaaa"] [into text! some 3 "a"])
]


(
    did all [
        "aaa" == uparse ["aaa"] [into text! [x: across some "a"]]
        x = "aaa"
    ]
)

(
    did all [
        "aaa" == uparse ["aaa"] [into <any> [x: across some "a"]]
        x = "aaa"
    ]
)

(
    did all [
        "aaa" == uparse "((aaa)))" [
            into [between some "(" some ")"] [x: across some "a"]
        ]
        x = "aaa"
    ]
)

(
    did all [
        [while while while] == uparse [| | while while while | | |] [
            content: between some '| some '|
            into (content) [x: collect [some keep ^['while]]]
        ]
        x = [while while while]
    ]
)

[(
    "" == uparse "baaabccc" [into [between "b" "b"] [some "a" <end>] to <end>]
)(
    didn't uparse "baaabccc" [into [between "b" "b"] ["a" <end>] to <end>]
)(
    didn't uparse "baaabccc" [into [between "b" "b"] ["a"] to <end>]
)(
    "" == uparse "baaabccc" [into [between "b" "b"] ["a" to <end>] "c" to <end>]
)(
    "" == uparse "aaabccc" [into [across to "b"] [some "a"] to <end>]
)]


; INTO can be mixed with HERE to parse into the same series
[(
    x: match-uparse "aaabbb" [
        some "a"
        into <here> ["bbb" (b: "yep, Bs")]
        "bbb" (bb: "Bs again")
    ]
    did all [
        x = "aaabbb"
        b = "yep, Bs"
        bb = "Bs again"
    ]
)(
    x: match-uparse "aaabbbccc" [
        some "a"
        into <here> ["bbb" to <end> (b: "yep, Bs")]
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

; INTO is not legal if a string uparse is already running
;
(error? trap [uparse "aa" [into ["a" "a"]]])

; Manual INTO via a recursion
[
    (none? uparse [a "test"] [
        'a s: text! (assert [#t == uparse s [4 <any>]])
    ])
]
