; Is PARSE working at all?

(did parse "abc" ["abc" end])

; Blank and empty block case handling

(did parse [] [end])
(did parse [] [[[]] end])
(did parse [] [_ _ _ end])
(not parse [x] [end])
(not parse [x] [_ _ _ end])
(not parse [x] [[[]] end])
(did parse [] [[[_ _ _] end]])
(did parse [x] ['x _ end])
(did parse [x] [_ 'x end])
(did parse [x] [[] 'x [] end])

; SET-WORD! (store current input position)

(
    res: did parse ser: [x y] [pos: skip skip end]
    all [res | pos = ser]
)
(
    res: did parse ser: [x y] [skip pos: skip end]
    all [res | pos = next ser]
)
(
    res: did parse ser: [x y] [skip skip pos: end]
    all [res | pos = tail of ser]
)
[#2130 (
    res: did parse ser: [x] [set val pos: word! end]
    all [res | val = 'x | pos = ser]
)]
[#2130 (
    res: did parse ser: [x] [set val: pos: word! end]
    all [res | val = 'x | pos = ser]
)]
[#2130 (
    res: did parse ser: "foo" [copy val pos: skip end]
    all [not res | val = "f" | pos = ser]
)]
[#2130 (
    res: did parse ser: "foo" [copy val: pos: skip end]
    all [not res | val = "f" | pos = ser]
)]

; TO/THRU integer!

(did parse "abcd" [to 3 "cd" end])
(did parse "abcd" [to 5 end])
(did parse "abcd" [to 128 end])

[#1965
    (did parse "abcd" [thru 3 "d" end])
]
[#1965
    (did parse "abcd" [thru 4 end])
]
[#1965
    (did parse "abcd" [thru 128 end])
]
[#1965
    (did parse "abcd" ["ab" to 1 "abcd" end])
]
[#1965
    (did parse "abcd" ["ab" thru 1 "bcd" end])
]

; parse THRU tag!

[#682 (
    t: _
    parse "<tag>text</tag>" [thru <tag> copy t to </tag> end]
    t == "text"
)]

; THRU advances the input position correctly.

(
    i: 0
    parse "a." [
        any [thru "a" (i: i + 1 j: to-value if i > 1 [[end skip]]) j]
        end
    ]
    i == 1
)

[#1959
    (did parse "abcd" [thru "d" end])
]
[#1959
    (did parse "abcd" [to "d" skip end])
]

[#1959
    (did parse "<abcd>" [thru <abcd> end])
]
[#1959
    (did parse [a b c d] [thru 'd end])
]
[#1959
    (did parse [a b c d] [to 'd skip end])
]

; self-invoking rule

[#1672 (
    a: [a end]
    error? trap [parse [] a]
)]

; repetition

[#1280 (
    parse "" [(i: 0) 3 [["a" |] (i: i + 1)] end]
    i == 3
)]
[#1268 (
    i: 0
    parse "a" [any [(i: i + 1)] end]
    i == 1
)]
[#1268 (
    i: 0
    parse "a" [while [(i: i + 1 j: to-value if i = 2 [[fail]]) j] end]
    i == 2
)]

; THEN rule

[#1267 (
    b: "abc"
    c: ["a" | "b"]
    a2: [any [b e: (d: [:e]) then fail | [c | (d: [fail]) fail]] d end]
    a4: [any [b then e: (d: [:e]) fail | [c | (d: [fail]) fail]] d end]
    equal? parse "aaaaabc" a2 parse "aaaaabc" a4
)]

; NOT rule

[#1246
    (did parse "1" [not not "1" "1" end])
]
[#1246
    (did parse "1" [not [not "1"] "1" end])
]
[#1246
    (not parse "" [not 0 "a" end])
]
[#1246
    (not parse "" [not [0 "a"] end])
]
[#1240
    (did parse "" [not "a" end])
]
[#1240
    (did parse "" [not skip end])
]
[#1240
    (did parse "" [not fail end])
]


; TO/THRU + bitset!/charset!

[#1457
    (did parse "a" compose [thru (charset "a") end])
]
[#1457
    (not parse "a" compose [thru (charset "a") skip end])
]
[#1457
    (did parse "ba" compose [to (charset "a") skip end])
]
[#1457
    (not parse "ba" compose [to (charset "a") "ba" end])
]

; self-modifying rule, not legal in Ren-C if it's during the parse

(error? trap [
    not parse "abcd" rule: ["ab" (remove back tail of rule) "cd" end]
])

(
    https://github.com/metaeducation/ren-c/issues/377
    o: make object! [a: 1]
    parse s: "a" [o/a: skip end]
    o/a = s
)

; A couple of tests for the problematic DO operation

(did parse [1 + 2] [do [quote 3] end])
(did parse [1 + 2] [do integer! end])
(did parse [1 + 2] [do [integer!] end])
(not parse [1 + 2] [do [quote 100] end])
(did parse [reverse copy [a b c]] [do [into ['c 'b 'a]] end])
(not parse [reverse copy [a b c]] [do [into ['a 'b 'c]] end])

; AHEAD and AND are synonyms
;
(did parse ["aa"] [ahead text! into ["a" "a"] end])
(did parse ["aa"] [and text! into ["a" "a"] end])

; INTO is not legal if a string parse is already running
;
(error? trap [parse "aa" [into ["a" "a"]] end])


; Should return the same series type as input (Rebol2 did not do this)
(
    a-value: first ['a/b]
    parse a-value [b-value: end]
    same? a-value b-value
)
(
    a-value: first [()]
    parse a-value [b-value: end]
    same? a-value b-value
)
(
    a-value: 'a/b
    parse a-value [b-value: end]
    same? a-value b-value
)
(
    a-value: first [a/b:]
    parse a-value [b-value: end]
    same? a-value b-value
)

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    parse "a" [any [(i: i + 1 j: if i = 2 [[end skip]]) j] end]
    i == 2
)

; Use MATCH to get input on success, see #2165
(
    "abc" = match parse "abc" ["a" "b" "c" end]
)
(
    null? match parse "abc" ["a" "b" "d" end]
)


;; DOUBLED GROUPS
;; Doubled groups inject their material into the PARSE, if it is not null.
;; They act like a COMPOSE/ONLY that runs each time the GROUP! is passed.

(did parse "aaabbb" [(([some "a"])) (([some "b"])) end])
(did parse "aaabbb" [(([some "a"])) ((if false [some "c"])) (([some "b"])) end])
(did parse "aaa" [(('some)) "a" end])
(not parse "aaa" [((1 + 1)) "a" end])
(did parse "aaa" [((1 + 2)) "a" end])
(
    count: 0
    did parse ["a" "aa" "aaa"] [some [into [((count: count + 1)) "a"]]]
)


; COLLECT and KEEP keywords
;
; Non-keyword COLLECT has issues with binding, but also does not have the
; necessary hook to be able to "backtrack" and remove kept material when a
; match rule containing keeps ultimately fails.  These keywords were initially
; introduced in Red, without backtracking...and affecting the return result.
; In Ren-C, backtracking is implemented, and also it is used to set variables
; (like a SET or COPY) instead of affecting the return result.

(all [
    parse [1 2 3] [collect x [keep [some integer!]]]
    x = [1 2 3]
])
(all [
    parse [1 2 3] [collect x [some [keep integer!]]]
    x = [1 2 3]
])
(all [
    parse [1 2 3] [collect x [keep only [some integer!]]]
    x = [[1 2 3]]
])
(all [
    parse [1 2 3] [collect x [some [keep only integer!]]]
    x = [[1] [2] [3]]
])

; Collecting non-array series fragments

(all [
    "bbb" = parse "aaabbb" [collect x [keep [some "a"]]]
    x = ["aaa"]
])
(all [
    "" = parse "aaabbbccc" [
        collect x [keep [some "a"] some "b" keep [some "c"]]
    ]
    x = ["aaa" "ccc"]
])

; Backtracking (more tests needed!)

(all [
    [] = parse [1 2 3] [
        collect x [
            keep integer! keep integer! keep text!
            |
            keep integer! keep [some integer!]
        ]
    ]
    x = [1 2 3]
])

; No change to variable on failed match (consistent with Rebol2/R3-Alpha/Red
; behaviors w.r.t SET and COPY)

(
    x: <before>
    all [
        null = parse [1 2] [collect x [keep integer! keep text!]]
        x = <before>
    ]
)

; Nested collect

(
    all [
        did parse [1 2 3 4] [
            collect a [
                keep integer!
                collect b [keep [2 integer!]]
                keep integer!
            ]
            end
        ]

        a = [1 4]
        b = [2 3]
    ]
)

; GET-BLOCK! can be used to keep material that did not originate from the
; input series or a match rule.  It does a REDUCE to more closely parallel
; the behavior of a GET-BLOCK! in the ordinary evaluator.
;
; !!! There is no GET-BLOCK! in the R3C branch, and patching it on would be
; somewhat prohibitive.
;
;(all [
;    [3] = parse [1 2 3] [
;        collect x [
;            keep integer!
;            keep :['a <b> #c]
;            keep integer!
;        ]
;    ]
;    x = [1 a <b> #c 2]
;])
;(all [
;    [3] = parse [1 2 3] [
;        collect x [
;            keep integer!
;            keep only :['a <b> #c]
;            keep integer!
;        ]
;    ]s
;    x = [1 [a <b> #c] 2]
;])
;(all [
;    parse [1 2 3] [collect x [keep only :[[a b c]]]]
;    x = [[[a b c]]]
;])
