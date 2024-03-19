; Is PARSE working at all?

(did parse/match "abc" ["abc" end])

; Blank and empty block case handling

(did parse/match [] [end])
(did parse/match [] [[[]]])
(did parse/match [_ _ _] [_ _ _ end])
(not parse/match [x] [end])
(not parse/match [x] [_ _ _])
(not parse/match [x] [[[]] end])
(did parse/match [_ _ _] [[[_ _ _] end]])
(did parse/match [x _] ['x _ end])
(did parse/match [_ x] [_ 'x end])
(did parse/match [x] [[] 'x []])

(did parse/match "" [])
(did parse/match "" [[[]] end])
(did parse/match "   " [_ _ _ end])
(not parse/match "x" [end])
(not parse/match "x" [_ _ _ end])
(not parse/match "x" [[[]]])
(did parse/match "   " [[[_ _ _] end]])
(did parse/match "x " ["x" _ end])
(did parse/match " x" [_ "x"])
(did parse/match "x" [[] "x" [] end])


; SET-WORD! (store current input position)

(
    res: did parse/match ser: [x y] [pos: <here> skip skip]
    all [res | pos = ser]
)
(
    res: did parse/match ser: [x y] [skip pos: <here> skip]
    all [res | pos = next ser]
)
(
    res: did parse/match ser: [x y] [skip skip pos: <here>]
    all [res | pos = tail of ser]
)
[#2130 (
    res: did parse/match ser: [x] [set val pos: <here> word!]
    all [res | val = 'x | pos = ser]
)]
[#2130 (
    res: did parse/match ser: [x] [set val: pos: <here> word!]
    all [res | val = 'x | pos = ser]
)]
[#2130 (
    res: did parse/match ser: "foo" [copy val pos: <here> skip]
    all [not res | val = "f" | pos = ser]
)]
[#2130 (
    res: did parse/match ser: "foo" [copy val: pos: <here> skip]
    all [not res | val = "f" | pos = ser]
)]

; TO/THRU integer!

(did parse/match "abcd" [to 3 "cd"])
(did parse/match "abcd" [to 5])
(did parse/match "abcd" [to 128])

[#1965
    (did parse/match "abcd" [thru 3 "d"])
]
[#1965
    (did parse/match "abcd" [thru 4])
]
[#1965
    (did parse/match "abcd" [thru 128])
]
[#1965
    (did parse/match "abcd" ["ab" to 1 "abcd"])
]
[#1965
    (did parse/match "abcd" ["ab" thru 1 "bcd"])
]

; parse/match THRU tag!

[#682 (
    t: _
    parse/match "<tag>text</tag>" [thru <tag> copy t to </tag>]
    t == "text"
)]

; THRU advances the input position correctly.

(
    i: 0
    parse/match "a." [
        any [thru "a" (i: i + 1 j: if i > 1 [[end skip]]) j]
    ]
    i == 1
)

[#1959
    (did parse/match "abcd" [thru "d"])
]
[#1959
    (did parse/match "abcd" [to "d" skip])
]

[#1959
    (did parse/match "<abcd>" [thru <abcd>])
]
[#1959
    (did parse/match [a b c d] [thru 'd])
]
[#1959
    (did parse/match [a b c d] [to 'd skip])
]

; self-invoking rule

[#1672 (
    a: [a end]
    error? trap [parse/match [] a]
)]

; repetition

[#1280 (
    parse/match "" [(i: 0) 3 [["a" |] (i: i + 1)]]
    i == 3
)]
[#1268 (
    i: 0
    parse/match "a" [any [(i: i + 1)]]
    i == 1
)]
[#1268 (
    i: 0
    parse/match "a" [while [(i: i + 1 j: if i = 2 [[fail]]) j]]
    i == 2
)]

; THEN rule

[#1267 (
    b: "abc"
    c: ["a" | "b"]
    a2: [any [b e: (d: [:e]) then fail | [c | (d: [fail]) fail]] d]
    a4: [any [b then e: (d: [:e]) fail | [c | (d: [fail]) fail]] d]
    equal? parse/match/redbol "aaaaabc" a2 parse/match/redbol "aaaaabc" a4
)]

; NOT rule

[#1246
    (did parse/match "1" [not not "1" "1"])
]
[#1246
    (did parse/match "1" [not [not "1"] "1"])
]
[#1246
    (not parse/match "" [not 0 "a"])
]
[#1246
    (not parse/match "" [not [0 "a"]])
]
[#1240
    (did parse/match "" [not "a"])
]
[#1240
    (did parse/match "" [not skip])
]
[#1240
    (did parse/match "" [not fail])
]


; TO/THRU + bitset!/charset!

[#1457
    (did parse/match "a" compose [thru (charset "a")])
]
[#1457
    (not parse/match "a" compose [thru (charset "a") skip])
]
[#1457
    (did parse/match "ba" compose [to (charset "a") skip])
]
[#1457
    (not parse/match "ba" compose [to (charset "a") "ba"])
]

; self-modifying rule, not legal in Ren-C if it's during the parse/match

(error? trap [
    not parse/match "abcd" rule: ["ab" (remove back tail of rule) "cd"]
])

(
    https://github.com/metaeducation/ren-c/issues/377
    o: make object! [a: 1]
    parse/match s: "a" [o/a: skip]
    o/a = s
)

; AHEAD and AND are synonyms
;
(did parse/match ["aa"] [ahead text! into ["a" "a"]])
(did parse/match ["aa"] [and text! into ["a" "a"]])

; INTO is not legal if a string parse/match is already running
;
(error? trap [parse/match "aa" [into ["a" "a"]]])


; Should return the same series type as input (Rebol2 did not do this)
(
    a-value: first ['a/b]
    parse/match a-value [b-value: <here>]
    same? a-value b-value
)
(
    a-value: first [()]
    parse/match a-value [b-value: <here>]
    same? a-value b-value
)
(
    a-value: 'a/b
    parse/match a-value [b-value: <here>]
    same? a-value b-value
)
(
    a-value: first [a/b:]
    parse/match a-value [b-value: <here>]
    same? a-value b-value
)

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    parse/match "a" [any [(i: i + 1 j: if i = 2 [[end skip]]) j]]
    i == 2
)


;; DOUBLED GROUPS
;; Doubled groups inject their material into the parse/match, if it is not null.
;; They act like a COMPOSE/ONLY that runs each time the GROUP! is passed.

(did parse/match "aaabbb" [(([some "a"])) (([some "b"]))])
(did parse/match "aaabbb" [(([some "a"])) ((if false [some "c"])) (([some "b"]))])
(did parse/match "aaa" [(('some)) "a"])
(not parse/match "aaa" [((1 + 1)) "a"])
(did parse/match "aaa" [((1 + 2)) "a"])
(
    count: 0
    did parse/match ["a" "aa" "aaa"] [some [into [((count: count + 1)) "a"]]]
)

(did parse/match "aaabbb" [some "a" foo: <here> some "b" seek foo some "b"])

(did parse/redbol/match "aaabbb" [some "a" foo: some "b" :foo some "b"])


; GET-BLOCK! can be used to keep material that did not originate from the
; input series or a match rule.  It does a REDUCE to more closely parallel
; the behavior of a GET-BLOCK! in the ordinary evaluator.
;
; !!! There is no GET-BLOCK! in the R3C branch, and patching it on would be
; somewhat prohibitive.
;
;(all [
;    [3] = parse/match [1 2 3] [
;        collect x [
;            keep integer!
;            keep :['a <b> #c]
;            keep integer!
;        ]
;    ]
;    x = [1 a <b> #c 2]
;])
;(all [
;    [3] = parse/match [1 2 3] [
;        collect x [
;            keep integer!
;            keep only :['a <b> #c]
;            keep integer!
;        ]
;    ]
;    x = [1 [a <b> #c] 2]
;])
;(all [
;    parse/match [1 2 3] [collect x [keep only :[[a b c]]]]
;    x = [[[a b c]]]
;])
