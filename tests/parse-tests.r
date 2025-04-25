; Is PARSE working at all?

(did parse/match "abc" ["abc" <end>])

; Blank and empty block case handling

(did parse/match [] [<end>])
(did parse/match [] [[[]]])
(did parse/match [_ _ _] [_ _ _ <end>])
(not parse/match [x] [<end>])
(not parse/match [x] [_ _ _])
(not parse/match [x] [[[]] <end>])
(did parse/match [_ _ _] [[[_ _ _] <end>]])
(did parse/match [x _] ['x _ <end>])
(did parse/match [_ x] [_ 'x <end>])
(did parse/match [x] [[] 'x []])

(did parse/match "" [])
(did parse/match "" [[[]] <end>])
(did parse/match "   " [_ _ _ <end>])
(not parse/match "x" [<end>])
(not parse/match "x" [_ _ _ <end>])
(not parse/match "x" [[[]]])
(did parse/match "   " [[[_ _ _] <end>]])
(did parse/match "x " ["x" _ <end>])
(did parse/match " x" [_ "x"])
(did parse/match "x" [[] "x" [] <end>])


; SET-WORD! (store current input position)

(
    res: did parse/match ser: [x y] [pos: <here> one one]
    all [res  pos = ser]
)
(
    res: did parse/match ser: [x y] [one pos: <here> one]
    all [res  pos = next ser]
)
(
    res: did parse/match ser: [x y] [one one pos: <here>]
    all [res  pos = tail of ser]
)

; These don't work with the new parse combinatorics
;
; [#2130 (
;    res: did parse/match ser: [x] [set val pos: <here> word!]
;    all [res | val = 'x | pos = ser]
; )]
; [#2130 (
;    res: did parse/match ser: [x] [set val: pos: <here> word!]
;    all [res | val = 'x | pos = ser]
; )]
; [#2130 (
;    res: did parse/match ser: "foo" [val: across pos: <here> one]
;    all [not ahead res | val = "f" | pos = ser]
; )]
; [#2130 (
;    res: did parse/match ser: "foo" [val: across pos: <here> one]
;    all [not ahead res | val = "f" | pos = ser]
; )]

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
    t: ~
    parse/match "<tag>text</tag>" [thru "<tag>" t: across to "</tag>"]
    t == "text"
)]

; THRU advances the input position correctly.

(
    i: 0
    parse/match "a." [
        opt some [thru "a" (i: i + 1 j: if i > 1 [[<end> one]]) j]
    ]
    i == 1
)

[#1959
    (did parse/match "abcd" [thru "d"])
]
[#1959
    (did parse/match "abcd" [to "d" one])
]

[#1959  ; TAG! no longer matches in string, interpreted as combinator
    (did parse/match "<abcd>" [thru "<abcd>"])
]
[#1959
    (did parse/match [a b c d] [thru 'd])
]
[#1959
    (did parse/match [a b c d] [to 'd one])
]

; self-invoking rule

[#1672 (
    a: [a <end>]
    error? sys/util/rescue [parse/match [] a]
)]

; repetition

[#1280 (
    parse/match "" [(i: 0) repeat 3 [["a" |] (i: i + 1)]]
    i == 3
)]

; Infinite loop in modern Ren-C (no progress requirement without FURTHER)
;[#1268 (
;    i: 0
;    parse/match "a" [opt some [(i: i + 1)]]
;    i == 1
;)]

[#1268 (
    i: 0
    parse/match "a" [opt some [(i: i + 1 j: if i = 2 [[bypass]]) j]]
    i == 2
)]

; THEN rule

[#1267 (
    b: "abc"
    c: ["a" | "b"]
    a2: [opt some [b e: (d: [:e]) then bypass | [c | (d: [bypass]) bypass]] d]
    a4: [opt some [b then e: (d: [:e]) bypass | [c | (d: [bypass]) bypass]] d]
    equal? parse/match/redbol "aaaaabc" a2 parse/match/redbol "aaaaabc" a4
)]

; NOT rule

[#1246
    (did parse/match "1" [not ahead not ahead "1" "1"])
]
[#1246
    (did parse/match "1" [not ahead [not ahead "1"] "1"])
]
[#1246
    (not parse/match "" [not ahead repeat 0 "a"])
]
[#1246
    (not parse/match "" [not ahead [repeat 0 "a"]])
]
[#1240
    (did parse/match "" [not ahead "a"])
]
[#1240
    (did parse/match "" [not ahead one])
]
[#1240
    (did parse/match "" [not ahead bypass])
]


; TO/THRU + bitset!/charset!

[#1457
    (did parse/match "a" compose [thru (charset "a")])
]
[#1457
    (not parse/match "a" compose [thru (charset "a") one])
]
[#1457
    (did parse/match "ba" compose [to (charset "a") one])
]
[#1457
    (not parse/match "ba" compose [to (charset "a") "ba"])
]

; self-modifying rule, not legal in Ren-C if it's during the parse/match

(error? sys/util/rescue [
    not parse/match "abcd" rule: ["ab" (remove back tail of rule) "cd"]
])

(
    https://github.com/metaeducation/ren-c/issues/377
    o: make object! [a: 1]
    parse/match s: "a" [o/a: one]
    o/a = s
)

; AHEAD and AND are synonyms
;
(did parse/match ["aa"] [ahead text! into ["a" "a"]])
(did parse/match ["aa"] [and text! into ["a" "a"]])

; INTO is not legal if a string parse/match is already running
;
(error? sys/util/rescue [parse/match "aa" [into ["a" "a"]]])


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
    parse/match "a" [opt some [(i: i + 1 j: if i = 2 [[<end> one]]) j]]
    i == 2
)


;; DOUBLED GROUPS
;; Doubled groups inject their material into the parse/match, if it is not null.
;; They act like a COMPOSE/ONLY that runs each time the GROUP! is passed.

(did parse/match "aaabbb" [(([some "a"])) (([some "b"]))])
(did parse/match "aaabbb" [(([some "a"])) ((if null [some "c"])) (([some "b"]))])
(did parse/match "aaa" [(('some)) "a"])

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


; The idea of being able to return a value from a parse is implemented via
; the ACCEPT combinator in UPARSE.  This was added to PARSE3.
(
    30 = parse "aaa" [some "a" accept (10 + 20)]
)
(
    pos: parse "abbbbbc" ["a" some ["b"] accept <here>]
    "c" = pos
)
(
    pos: parse "abbbbc" ["ab" some ["bc" | "b"] accept <here>]
    "" = pos
)
(
    pos: parse "abc10def" ["abc" "10" accept <here>]
    "def" = pos
)
