; Is PARSE working at all?

(did parse "abc" ["abc"])
(did [_ pos]: parse "abc" ["abc" end])

; Edge case of matching END with TO or THRU
;
("" = parse "" [to ["a" | end]])
("" = parse "" [thru ["a" | end]])
([] = parse [] [to ["a" | end]])
([] = parse [] [thru ["a" | end]])


[#206 (
    any-char: complement charset ""
    repeat n 512 [
        if n = 1 [continue]

        if not parse (append copy "" make char! n - 1) [set c any-char end] [
            fail "Parse didn't work"
        ]
        if c != make char! n - 1 [fail "Char didn't match"]
    ]
    true
)]


; Don't leak internal detail that BINARY! or ANY-STRING! are 0-terminated
[
    (NUL = as issue! 0)

    (null = parse "" [to NUL])
    (null = parse "" [thru NUL])
    (null = parse "" [to [NUL]])
    (null = parse "" [thru [NUL]])

    (null = parse #{} [to NUL])
    (null = parse #{} [thru NUL])
    (null = parse #{} [to [NUL]])
    (null = parse #{} [thru [NUL]])
]


; Plain voids cause an error, quoted voids match literal voids
(
    foo: '~void~
    e: trap [parse "a" [foo]]
    e/id = 'need-non-void
)(
    foo: quote '~void~
    did parse [~void~] [foo end]
)

; Empty block case handling

(did parse [] [])
(did parse [] [[[]]])
(not parse [x] [])
(not parse [x] [[[]]])
(did parse [x] [[] 'x []])

; Literal blank vs. fetched blank/null handling.
; Literal blank means "skip" at source level, but if retrieved from a variable
; it means the same as null.
; https://forum.rebol.info/t/1348
[
    (did parse [x] ['x null])
    (did [_ pos]: parse [x] [blank 'x end])

    (did parse [] [blank blank blank])
    (not parse [] [_ _ _])
    (did parse [x <y> "z"] [_ _ _])

    (not parse [x <y> "z"] ['_ '_ '_])
    (did parse [_ _ _] ['_ '_ '_])
    (
        q-blank: quote _
        did parse [_ _ _] [q-blank q-blank q-blank]
    )

    (not parse [] [[[_ _ _]]])
    (did parse [] [[[blank blank blank]]])
    (did parse [] [[[null null null]]])
]

; SET-WORD! (store current input position)

(
    res: did parse ser: [x y] [pos: here, skip, skip]
    all [res, pos = ser]
)
(
    res: did parse ser: [x y] [skip, pos: here, skip]
    all [res, pos = next ser]
)
(
    res: did parse ser: [x y] [skip, skip, pos: here]
    all [res, pos = tail of ser]
)
[#2130 (
    res: did parse ser: [x] [pos: here, set val word!]
    all [res, val = 'x, pos = ser]
)]
[#2130 (
    res: did parse ser: [x] [pos: here, set val: word!]
    all [res, val = 'x, pos = ser]
)]
[#2130 (
    res: did parse ser: "foo" [pos: here, copy val skip]
    all [not res, val = "f", pos = ser]
)]
[#2130 (
    res: did parse ser: "foo" [pos: here, copy val: skip]
    all [not res, val = "f", pos = ser]
)]

; SEEK INTEGER! (replaces TO/THRU integer!

(did parse "abcd" [seek 3 "cd"])
(did parse "abcd" [seek 5])
(did parse "abcd" [seek 128])

[#1965
    (did parse "abcd" [seek 3 skip "d"])
    (did parse "abcd" [seek 4 skip])
    (did parse "abcd" [seek 128])
    (did parse "abcd" ["ab" seek 1 "abcd"])
    (did parse "abcd" ["ab" seek 1 skip "bcd"])
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
    parse "a." [
        any [thru "a" (i: i + 1 j: to-value if i > 1 [[end skip]]) j]
        end
    ]
    i == 1
)

[#1959
    (did parse "abcd" [thru "d"])
]
[#1959
    (did parse "abcd" [to "d" skip])
]

[#1959
    (did parse "<abcd>" [thru <abcd>])
]
[#1959
    (did parse [a b c d] [thru 'd])
]
[#1959
    (did parse [a b c d] [to 'd skip])
]

; self-invoking rule

[#1672 (
    a: [a]
    error? trap [parse [] a]
)]

; repetition

[#1280 (
    parse "" [(i: 0) 3 [["a" |] (i: i + 1)]]
    i == 3
)]
[#1268 (
    i: 0
    <infinite?> = catch [
        parse "a" [any [(i: i + 1) (if i > 100 [throw <infinite?>])]]
    ]
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
    a2: [any [b, e: here, (d: [seek e]) then fail | [c | (d: [fail]) fail]] d]
    a4: [any [b then e: here (d: [seek e]) fail | [c | (d: [fail]) fail]] d]
    equal? parse "aaaaabc" a2 parse "aaaaabc" a4
)]

; NOT rule

[#1246
    (did parse "1" [not not "1" "1"])
]
[#1246
    (did parse "1" [not [not "1"] "1"])
]
[#1246
    (not parse "" [not 0 "a"])
]
[#1246
    (not parse "" [not [0 "a"]])
]
[#1240
    (did parse "" [not "a"])
]
[#1240
    (did parse "" [not skip])
]
[#1240
    (did parse "" [not fail])
]


; TO/THRU + bitset!/charset!

[#1457
    (did parse "a" compose [thru (charset "a")])
]
[#1457
    (not parse "a" compose [thru (charset "a") skip])
]
[#1457
    (did parse "ba" compose [to (charset "a") skip])
]
[#1457
    (not parse "ba" compose [to (charset "a") "ba"])
]
[#2141 (
    xset: charset "x"
    did parse "x" [thru [xset]]
)]

; self-modifying rule, not legal in Ren-C if it's during the parse

(error? trap [
    not parse "abcd" rule: ["ab" (remove back tail of rule) "cd"]
])

[https://github.com/metaeducation/ren-c/issues/377 (
    o: make object! [a: 1]
    parse s: "a" [o/a: skip]
    o/a = s
)]

; AHEAD and AND are synonyms
;
(did parse ["aa"] [ahead text! into ["a" "a"]])
(did parse ["aa"] [and text! into ["a" "a"]])

; INTO is not legal if a string parse is already running
;
(error? trap [parse "aa" [into ["a" "a"]]])


; Should return the same series type as input (Rebol2 did not do this)
; PATH! cannot be PARSE'd due to restrictions of the implementation
(
    a-value: first [a/b]
    parse as block! a-value [b-value: here]
    a-value = to path! b-value
)
(
    a-value: first [()]
    parse a-value [b-value: here]
    same? a-value b-value
)

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    parse "a" [any [(i: i + 1 j: if i = 2 [[end skip]]) j]]
    i == 2
)


; GET-GROUP!
; These evaluate and inject their material into the PARSE, if it is not null.
; They act like a COMPOSE/ONLY that runs each time the GET-GROUP! is passed.

(did parse "aaabbb" [:([some "a"]) :([some "b"])])
(did parse "aaabbb" [:([some "a"]) :(if false [some "c"]) :([some "b"])])
(did parse "aaa" [:('some) "a"])
(not parse "aaa" [:(1 + 1) "a"])
(did parse "aaa" [:(1 + 2) "a"])
(
    count: 0
    did parse ["a" "aa" "aaa"] [some [into [:(count: count + 1) "a"]]]
)

; SET-GROUP!
; What these might do in PARSE could be more ambitious, but for starters they
; provide a level of indirection in SET.

(
    m: null
    word: 'm
    did all [
        parse [1020] [(word): integer!]
        word = 'm
        m = 1020
    ]
)

; LOGIC! BEHAVIOR
; A logic true acts as a no-op, while a logic false causes matches to fail

(did parse "ab" ["a" true "b"])
(not parse "ab" ["a" false "b"])
(did parse "ab" ["a" :(1 = 1) "b"])
(not parse "ab" ["a" :(1 = 2) "b"])


; QUOTED! BEHAVIOR
; Support for the new literal types

(
    did all [
        [_ pos]: parse [... [a b]] [to '[a b]]
        pos = [[a b]]
    ]
)
(did parse [... [a b]] [thru '[a b]])
(did parse [1 1 1] [some '1])

; Quote level is not retained by captured content
;
(did all [
    [_ pos]: parse [''[1 + 2]] [into [copy x to end]]
    [] == pos
    x == [1 + 2]
])


; As alternatives to using SET-WORD! to set the parse position and GET-WORD!
; to get the parse position, Ren-C has keywords HERE and SEEK.  HERE has
; precedent in Topaz:
;
; https://github.com/giesse/red-topaz-parse
;
; Unlike R3-Alpha, changing the series being parsed is not allowed.
(
    did all [
        parse "aabbcc" [
            some "a", x: here, some "b", y: here
            seek x, copy z to end
        ]
        x = "bbcc"
        y = "cc"
        z = "bbcc"
    ]
)(
    pos: 5
    parse "123456789" [seek pos copy nums to end]
    nums = "56789"
)


; Multi-byte characters and strings present a lot of challenges.  There should
; be many more tests and philosophies written up of what the semantics are,
; especially when it comes to BINARY! and ANY-STRING! mixtures.  These tests
; are better than nothing...
(
    catchar: #"🐱"
    did parse #{F09F90B1} [catchar]
)(
    cattext: "🐱"
    did parse #{F09F90B1} [cattext]
)(
    catbin: #{F09F90B1}
    e: trap [did parse "🐱" [catbin]]
    'find-string-binary = e/id
)(
    catchar: #"🐱"
    did parse "🐱" [catchar]
)

[
    (
        bincat: to-binary {C😺T}
        bincat = #{43F09F98BA54}
    )

    (did parse bincat [{C😺T}])

    (did parse bincat [{c😺t}])

    (not parse/case bincat [{c😺t} end])
]

(
    test: to-binary {The C😺T Test}
    did all [
        parse test [to {c😺t} copy x to space to end]
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)


; With UTF-8 Everywhere, all strings are kept in UTF-8 format all the time.
; This makes it feasible to invoke the scanner during PARSE of a TEXT!.
; The feature was added only as a quick demo and needs significant design,
; testing, and development.  But it was added to show what kinds of things
; are possible now.

(did all [
    parse "abc [d e f] {Hello}" ["abc" set b block! set s text!]
    b = [d e f]
    s = {Hello}
])


(did all [
    parse text: "a ^/ " [
        any [newline remove [to end] | "a" [remove [to newline]] | skip]
    ]
    text = "a^/"
])


(did parse "a" [some [to end] end])

[https://github.com/metaeducation/ren-c/issues/1032 (
    s: {abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ}
    t: {----------------------------------------------------}
    for n 2 50 1 [
        sub: copy/part s n
        parse sub [any [
            remove skip
            insert "-"
        ]]
        if sub != copy/part t n [fail "Incorrect Replacement"]
    ]
    true
)]

[(
    countify: function [things data] [
        counts: make map! []
        rules: collect [
            for-each t things [
                counts/(t): 0
                keep t
                keep/only compose/deep '(counts/(t): me + 1)
                keep/line '|
            ]
            keep/only 'fail
        ]
        parse data (compose/deep [
            any [((rules))]
            end
        ]) then [
            collect [
                for-each [key value] counts [
                    keep key
                    keep value
                ]
            ]
        ] else [
            <outlier>
        ]
    ]
    true
)(
    ["a" 3 "b" 3 "c" 3] = countify ["a" "b" "c"] "aaabccbbc"
)(
    <outlier> = countify ["a" "b" "c"] "aaabccbbcd"
)]

[
    https://github.com/rebol/rebol-issues/issues/2393
    (not parse "aa" [some [#"a"] reject])
    (not parse "aabb" [some [#"a"] reject some [#"b"]])
    (not parse "aabb" [some [#"a" reject] to end])
]

; !!! R3-Alpha introduced a controversial "must make progress" rule, where
; something like an empty string does not make progress on a string parse
; so even if it doesn't fail, it fails the whole parse.  Red has all of
; these tests pass.  Ren-C is questioning the progress rule, believing the
; benefit of infinite-loop-avoidance is not worth the sacrifice of logic.
[
    (not parse "ab" [to [""] "ab"])
    (did parse "ab" [to ["a"] "ab"])
    (did parse "ab" [to ["ab"] "ab"])
    (not parse "ab" [thru [""] "ab"])
    (did parse "ab" [thru ["a"] "b"])
    (not parse "ab" [thru ["ab"] ""])
]

; Ren-C made it possible to use quoted WORD!s in place of CHAR! or TEXT! to
; match in strings.  This gives a cleaner look, as you drop off 3 vertical
; tick marks from everything like ["ab"] to become just ['ab]
;
(did all [
    [_ pos]: parse "abbbbbc" ['a some ['b]]
    "c" = pos
])
(did all [
    [_ pos]: parse "abbbbc" ['ab some ['bc | 'b]]
    "" = pos
])
(did all [
    [_ pos]: parse "abc10def" ['abc '10]
    "def" = pos
])

(
    byteset: make bitset! [0 16 32]
    did parse #{001020} [some byteset]
)

; A SET of zero elements gives NULL, a SET of > 1 elements is an error
[(
    x: <before>
    did all [
        [1] = parse [1] [set x opt text! integer!]
        x = null
    ]
)(
    x: <before>
    did all [
        ["a" 1] = parse ["a" 1] [set x some text! integer!]
        x = "a"
    ]
)(
    x: <before>
    e: trap [
        ["a" "b" 1] = parse ["a" "b" 1] [set x some text! integer!]
    ]
    did all [
        e/id = 'parse-multiple-set
        x = <before>
    ]
)]
