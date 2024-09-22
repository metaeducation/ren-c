; %parse-subparse.test.reb
;
; SUPARSE is an arity-2 of historical Rebol PARSE INTO, permitting use of a
; value-bearing rule to produce the thing to recurse the parser into...which
; can generate a new series, as well as pick one out of a block.

[
    ; !!! To limit the scope of potential vaporization, only a few combinators
    ; pass it through.  The block combinator does, so `[elide "a"]` acts the
    ; same as `elide "a"`.  But SUBPARSE currently does not, and leverages
    ; the default behavior that functions which do not support NIHIL returns
    ; will react by producing a VOID when passed a NIHIL.
    ;
    (void? parse [[]] [subparse &any-series? []])

    ('a == parse [[a]] [subparse &any-series? ['a]])
    ('c == parse [b [a] c] ['b subparse &any-series? ['a] 'c])
    (#a == parse ["a"] [subparse &any-series? [#a]])
    ('c == parse [b "a" c] ['b subparse &any-series? ["a"] 'c])
    (#a == parse [["a"]] [subparse block! [subparse &any-series? [#a]]])

    ~parse-mismatch~ !! (parse [[a]] [subparse &any-series? ['a 'b]])
    ~parse-mismatch~ !! (parse [[a]] [subparse &any-series? [some 'b]])

    ([a] == parse [[a]] [subparse &any-series? ['a 'b] | block!])
]

("a" == parse ["aa"] [subparse text! ["a" "a"]])

; One key feature of UPARSE is that rule chaining is done in such a way that
; it delegates the recognition to the parse engine, meaning that rules do not
; have to be put into blocks as often.
[
    ("a" = parse ["aaaa"] [subparse text! some repeat 2 "a"])
    ~parse-mismatch~ !! (parse ["aaaa"] [subparse text! some repeat 3 "a"])
]


(
    all [
        "aaa" == parse ["aaa"] [subparse text! [x: across some "a"]]
        x = "aaa"
    ]
)

(
    all [
        "aaa" == parse ["aaa"] [subparse one [x: across some "a"]]
        x = "aaa"
    ]
)

(
    all [
        "aaa" == parse "((aaa)))" [
            subparse [between some "(" some ")"] [x: across some "a"]
        ]
        x = "aaa"
    ]
)

(
    all [
        [some some some] == parse [| | some some some | | |] [
            content: between some '| some '|
            subparse (content) [x: collect [some keep ['some]]]
        ]
        x = [some some some]
    ]
)

[(
    "a" == parse "baaabccc" [
        subparse [between "b" "b"] [some "a" <end>] to <end>
    ]
)

~parse-mismatch~ !! (
    parse "baaabccc" [
        subparse [between "b" "b"] ["a" <end>], to <end>
    ]
)

~parse-mismatch~ !! (
    parse "baaabccc" [subparse [between "b" "b"] ["a"], to <end>]
)

(
    "c" == parse "baaabccc" [
        subparse [between "b" "b"] ["a" to <end>], "c", to <end>
    ]
)(
    "a" == parse "aaabccc" [subparse [across to "b"] [some "a"], to <end>]
)]


; SUBPARSE can be mixed with HERE to parse into the same series
;
; Note: If functions with INPUT that return progress would act implicitly as
; combinators, then SUBPARSE <HERE> would be how PARSE would act.
[(
    x: parse "aaabbb" [
        some "a"
        subparse <here> ["bbb" (b: "yep, Bs")]
        "bbb" (bb: "Bs again")
    ]
    all [
        x = "Bs again"
        b = "yep, Bs"
        bb = "Bs again"
    ]
)(
    x: parse "aaabbbccc" [
        some "a"
        subparse <here> ["bbb" to <end> (b: "yep, Bs")]
        "bbb" (bb: "Bs again")
        "ccc" (c: "Here be Cs")
    ]
    all [
        x = "Here be Cs"
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
        'a s: text! (assert [#t == parse s [repeat 4 one]])
    ])
]

; Parsing ANY-SEQUENCE is allowed
(
    'c = parse [/a/c/] [subparse path! [_ 'a 'c elide _]]
)
