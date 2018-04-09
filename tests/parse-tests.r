; Is PARSE working at all?

(parse "abc" ["abc"])

; Blank and empty block case handling

(parse [] [])
(parse [] [[[]]])
(parse [] [_ _ _])
(not parse [x] [])
(not parse [x] [_ _ _])
(not parse [x] [[[]]])
(parse [] [[[_ _ _]]])
(parse [x] ['x _])
(parse [x] [_ 'x])
(parse [x] [[] 'x []])

; SET-WORD! (store current input position)

(
    res: parse ser: [x y] [pos: skip skip]
    all [res | pos = ser]
)
(
    res: parse ser: [x y] [skip pos: skip]
    all [res | pos = next ser]
)
(
    res: parse ser: [x y] [skip skip pos: end]
    all [res | pos = tail of ser]
)
[#2130 (
    res: parse ser: [x] [set val pos: word!]
    all [res | val = 'x | pos = ser]
)]
[#2130 (
    res: parse ser: [x] [set val: pos: word!]
    all [res | val = 'x | pos = ser]
)]
[#2130 (
    res: parse
    ser: "foo" [copy val pos: skip]
    all [not res | val = "f" | pos = ser]
)]
[#2130 (
    res: parse ser: "foo" [copy val: pos: skip]
    all [not res | val = "f" | pos = ser]
)]

; TO/THRU integer!

(parse "abcd" [to 3 "cd"])
(parse "abcd" [to 5])
(parse "abcd" [to 128])

[#1965
    (parse "abcd" [thru 3 "d"])
]
[#1965
    (parse "abcd" [thru 4])
]
[#1965
    (parse "abcd" [thru 128])
]
[#1965
    (parse "abcd" ["ab" to 1 "abcd"])
]
[#1965
    (parse "abcd" ["ab" thru 1 "bcd"])
]

; parse THRU tag!

[#682 (
    t: _
    parse "<tag>text</tag>" [thru <tag> copy t to </tag>]
    t == "text"
)]

; THRU advances the input position correctly.

(
    i: 0
    parse "a." [any [thru "a" (i: i + 1 j: to-value if i > 1 [[end skip]]) j]]
    i == 1
)

[#1959
    (parse "abcd" [thru "d"])
]
[#1959
    (parse "abcd" [to "d" skip])
]

[#1959
    (parse "<abcd>" [thru <abcd>])
]
[#1959
    (parse [a b c d] [thru 'd])
]
[#1959
    (parse [a b c d] [to 'd skip])
]

; self-invoking rule

[#1672 (
    a: [a]
    error? try [parse [] a]
)]

; repetition

[#1280 (
    parse "" [(i: 0) 3 [["a" |] (i: i + 1)]]
    i == 3
)]
[#1268 (
    i: 0
    parse "a" [any [(i: i + 1)]]
    i == 1
)]
[#1268 (
    i: 0
    parse "a" [while [(i: i + 1 j: to-value if i = 2 [[fail]]) j]]
    i == 2
)]

; THEN rule

[#1267 (
    b: "abc"
    c: ["a" | "b"]
    a2: [any [b e: (d: [:e]) then fail | [c | (d: [fail]) fail]] d]
    a4: [any [b then e: (d: [:e]) fail | [c | (d: [fail]) fail]] d]
    equal? parse "aaaaabc" a2 parse "aaaaabc" a4
)]

; NOT rule

[#1246
    (parse "1" [not not "1" "1"])
]
[#1246
    (parse "1" [not [not "1"] "1"])
]
[#1246
    (not parse "" [not 0 "a"])
]
[#1246
    (not parse "" [not [0 "a"]])
]
[#1240
    (parse "" [not "a"])
]
[#1240
    (parse "" [not skip])
]
[#1240
    (parse "" [not fail])
]

[#100
    (1 == eval does [parse [] [(return 1)] 2])
]

; TO/THRU + bitset!/charset!

[#1457
    (parse "a" compose [thru (charset "a")])
]
[#1457
    (not parse "a" compose [thru (charset "a") skip])
]
[#1457
    (parse "ba" compose [to (charset "a") skip])
]
[#1457
    (not parse "ba" compose [to (charset "a") "ba"])
]

; self-modifying rule, not legal in Ren-C if it's during the parse

(error? try [not parse "abcd" rule: ["ab" (remove back tail of rule) "cd"]])

(
    https://github.com/metaeducation/ren-c/issues/377
    o: make object! [a: 1]
    true = parse "a" [o/a: skip]
)

; A couple of tests for the problematic DO operation

(parse [1 + 2] [do [quote 3]])
(parse [1 + 2] [do integer!])
(parse [1 + 2] [do [integer!]])
(not parse [1 + 2] [do [quote 100]])
(parse [reverse copy [a b c]] [do [into ['c 'b 'a]]])
(not parse [reverse copy [a b c]] [do [into ['a 'b 'c]]])

; AHEAD and AND are synonyms
;
(parse ["aa"] [ahead string! into ["a" "a"]])
(parse ["aa"] [and string! into ["a" "a"]])

; INTO is not legal if a string parse is already running
;
(error? trap [parse "aa" [into ["a" "a"]]])


; Should return the same series type as input (Rebol2 did not do this)
(
    a-value: first ['a/b]
    parse a-value [b-value:]
    same? a-value b-value
)
(
    a-value: first [()]
    parse a-value [b-value:]
    same? a-value b-value
)
(
    a-value: 'a/b
    parse a-value [b-value:]
    same? a-value b-value
)
(
    a-value: first [a/b:]
    parse a-value [b-value:]
    same? a-value b-value
)

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    parse "a" [any [(i: i + 1 j: if i = 2 [[end skip]]) j]]
    i == 2
)

; Variant which returns the input instead of a LOGIC! on success, may need
; a better name, see #2165
(
    "abc" = parse-match "abc" ["a" "b" "c"]
)
(
    blank? parse-match "abc" ["a" "b" "d"]
)
