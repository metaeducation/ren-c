; Is PARSE working at all?

(did parse3 "abc" ["abc"])
(did parse3 "abc" ["abc" <end>])

; Edge case of matching <END> with TO or THRU
;
(did parse3 "" [to ["a" | <end>]])
(did parse3 "" [thru ["a" | <end>]])
(did parse3 [] [to ["a" | <end>]])
(did parse3 [] [thru ["a" | <end>]])


[#206 (
    any-char: complement charset ""
    count-up n 512 [
        if n = 1 [continue]

        if didn't parse3 (append copy "" make char! n - 1) [set c any-char <end>] [
            fail "Parse didn't work"
        ]
        if c != make char! n - 1 [fail "Char didn't match"]
    ]
    true
)]

(
    var: 3
    rule: "a"
    did parse3 "aaa" [repeat (var) rule]  ; clearer than [var rule]
)

; Don't leak internal detail that BINARY! or ANY-STRING! are 0-terminated
[
    (NUL = as issue! 0)

    (didn't parse3 "" [to NUL])
    (didn't parse3 "" [thru NUL])
    (didn't parse3 "" [to [NUL]])
    (didn't parse3 "" [thru [NUL]])

    (didn't parse3 #{} [to NUL])
    (didn't parse3 #{} [thru NUL])
    (didn't parse3 #{} [to [NUL]])
    (didn't parse3 #{} [thru [NUL]])
]


; BAD-WORD! isotopes cause an error, plain BAD-WORD! matches literal BAD-WORDs
(
    foo: ~none~
    e: trap [parse3 "a" [foo]]
    e.id = 'bad-word-get
)(
    foo: '~none~
    did parse3 [~none~] [foo <end>]
)

; Empty block case handling

(did parse3 [] [])
(did parse3 [] [[[]]])
(didn't parse3 [x] [])
(didn't parse3 [x] [[[]]])
(did parse3 [x] [[] 'x []])

; No longer contentious concept: NULL is not legal as a parse rule.
;
; Contentious concept: PARSE3 tried the idea that literal blank would be
; different from fetched blank.  Literal blank means "skip" at source level,
; but if retrieved from a variable it means the same as empty block.
;
; https://forum.rebol.info/t/1348
[
    (error? trap [did parse3 [x] ['x null]])
    (did parse3 [x] [blank 'x <end>])

    (did parse3 [] [blank blank blank])
    (didn't parse3 [] [_ _ _])
    (did parse3 [x <y> "z"] [_ _ _])

    (didn't parse3 [x <y> "z"] ['_ '_ '_])
    (did parse3 [_ _ _] ['_ '_ '_])
    (
        q-blank: quote _
        did parse3 [_ _ _] [q-blank q-blank q-blank]
    )

    (didn't parse3 [] [[[_ _ _]]])
    (did parse3 [] [[[blank blank blank]]])
]

; SET-WORD! (store current input position)

(
    res: did parse3 ser: [x y] [pos: <here>, skip, skip]
    all [res, pos = ser]
)
(
    res: did parse3 ser: [x y] [skip, pos: <here>, skip]
    all [res, pos = next ser]
)
(
    res: did parse3 ser: [x y] [skip, skip, pos: <here>]
    all [res, pos = tail of ser]
)
[#2130 (
    res: did parse3 ser: [x] [pos: <here>, set val word!]
    all [res, val = 'x, pos = ser]
)]
[#2130 (
    res: did parse3 ser: [x] [pos: <here>, set val: word!]
    all [res, val = 'x, pos = ser]
)]
[#2130 (
    res: did parse3 ser: "foo" [pos: <here>, copy val skip]
    all [not res, val = "f", pos = ser]
)]
[#2130 (
    res: did parse3 ser: "foo" [pos: <here>, copy val: skip]
    all [not res, val = "f", pos = ser]
)]

; SEEK INTEGER! (replaces TO/THRU integer!

(did parse3 "abcd" [seek 3 "cd"])
(did parse3 "abcd" [seek 5])
(did parse3 "abcd" [seek 128])

[#1965
    (did parse3 "abcd" [seek 3 skip "d"])
    (did parse3 "abcd" [seek 4 skip])
    (did parse3 "abcd" [seek 128])
    (did parse3 "abcd" ["ab" seek 1 "abcd"])
    (did parse3 "abcd" ["ab" seek 1 skip "bcd"])
]

; parse THRU tag!

[#682 (
    t: _
    parse3 "<tag>text</tag>" [thru '<tag> copy t to '</tag>]
    t == "text"
)]

; THRU advances the input position correctly.

(
    i: 0
    parse3 "a." [
        while [thru "a" (i: i + 1 j: try if i > 1 [<end> skip]) j]
    ]
    i == 1
)

[#1959
    (did parse3 "abcd" [thru "d"])
]
[#1959
    (did parse3 "abcd" [to "d" skip])
]

[#1959
    (did parse3 "<abcd>" [thru '<abcd>])
]
[#1959
    (did parse3 [a b c d] [thru 'd])
]
[#1959
    (did parse3 [a b c d] [to 'd skip])
]

; self-invoking rule

[#1672 (
    a: [a]
    error? trap [parse3 [] a]
)]

; repetition

[#1280 (
    parse3 "" [(i: 0) 3 [["a" |] (i: i + 1)]]
    i == 3
)]
[#1268 (
    i: 0
    <infinite?> = catch [
        parse3 "a" [while [(i: i + 1) (if i > 100 [throw <infinite?>])]]
    ]
)]
[#1268 (
    i: 0
    parse3 "a" [while [(i: i + 1 j: try if i = 2 [[fail]]) j]]
    i == 2
)]

; NOT rule

[#1246
    (did parse3 "1" [not not "1" "1"])
]
[#1246
    (did parse3 "1" [not [not "1"] "1"])
]
[#1246
    (didn't parse3 "" [not 0 "a"])
]
[#1246
    (didn't parse3 "" [not [0 "a"]])
]
[#1240
    (did parse3 "" [not "a"])
]
[#1240
    (did parse3 "" [not skip])
]
[#1240
    (did parse3 "" [not fail])
]


; TO/THRU + bitset!/charset!

[#1457
    (did parse3 "a" compose [thru (charset "a")])
]
[#1457
    (didn't parse3 "a" compose [thru (charset "a") skip])
]
[#1457
    (did parse3 "ba" compose [to (charset "a") skip])
]
[#1457
    (didn't parse3 "ba" compose [to (charset "a") "ba"])
]
[#2141 (
    xset: charset "x"
    did parse3 "x" [thru [xset]]
)]

; self-modifying rule, not legal in Ren-C if it's during the parse

(error? trap [
    didn't parse3 "abcd" rule: ["ab" (remove back tail of rule) "cd"]
])

[https://github.com/metaeducation/ren-c/issues/377 (
    o: make object! [a: 1]
    parse3 s: "a" [o.a: here, skip]
    o.a = s
)]

; AHEAD and AND are synonyms
;
(did parse3 ["aa"] [ahead text! into ["a" "a"]])
(did parse3 ["aa"] [and text! into ["a" "a"]])

[#1238
    (null = parse3 "ab" [ahead "ab" "ac"])
    (null = parse3 "ac" [ahead "ab" "ac"])
]

; INTO is not legal if a string parse is already running
;
(error? trap [parse3 "aa" [into ["a" "a"]]])


; Should return the same series type as input (Rebol2 did not do this)
; PATH! cannot be PARSE'd due to restrictions of the implementation
(
    a-value: first [a/b]
    parse3 as block! a-value [b-value: <here>]
    a-value = to path! b-value
)
(
    a-value: first [()]
    parse3 a-value [b-value: <here>]
    same? a-value b-value
)

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    parse3 "a" [while [(i: i + 1 j: if i = 2 [[<end> skip]]) j]]
    i == 2
)


; GET-GROUP!
; These evaluate and inject their material into the PARSE, if it is not null.
; They act like a COMPOSE/ONLY that runs each time the GET-GROUP! is passed.

(did parse3 "aaabbb" [:([some "a"]) :([some "b"])])
(did parse3 "aaabbb" [:([some "a"]) :(if false [some "c"]) :([some "b"])])
(did parse3 "aaa" [:('some) "a"])
(didn't parse3 "aaa" [:(1 + 1) "a"])
(did parse3 "aaa" [:(1 + 2) "a"])
(
    count: 0
    did parse3 ["a" "aa" "aaa"] [some [into [:(count: count + 1) "a"]]]
)

; SET-GROUP!
; What these might do in PARSE could be more ambitious, but for starters they
; provide a level of indirection in SET.

(
    m: ~
    word: 'm
    did all [
        did parse3 [1020] [(word): integer!]
        word = 'm
        m = 1020
    ]
)

; LOGIC! BEHAVIOR
; A logic true acts as a no-op, while a logic false causes matches to fail

(did parse3 "ab" ["a" true "b"])
(didn't parse3 "ab" ["a" false "b"])
(did parse3 "ab" ["a" :(1 = 1) "b"])
(didn't parse3 "ab" ["a" :(1 = 2) "b"])


; QUOTED! BEHAVIOR
; Support for the new literal types

(
    did all [
        pos: parse* [... [a b]] [to '[a b], <here>]
        pos = [[a b]]
    ]
)
(did parse3 [... [a b]] [thru '[a b]])
(did parse3 [1 1 1] [some '1])

; Quote level is not retained by captured content
;
(did all [
    pos: parse* [''[1 + 2]] [into [copy x to <end>], <here>]
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
        did parse3 "aabbcc" [
            some "a", x: <here>, some "b", y: <here>
            seek x, copy z to <end>
        ]
        x = "bbcc"
        y = "cc"
        z = "bbcc"
    ]
)(
    pos: 5
    parse3 "123456789" [seek pos copy nums to <end>]
    nums = "56789"
)


; Multi-byte characters and strings present a lot of challenges.  There should
; be many more tests and philosophies written up of what the semantics are,
; especially when it comes to BINARY! and ANY-STRING! mixtures.  These tests
; are better than nothing...
(
    catchar: #"🐱"
    did parse3 #{F09F90B1} [catchar]
)(
    cattext: "🐱"
    did parse3 #{F09F90B1} [cattext]
)(
    catbin: #{F09F90B1}
    e: trap [did parse3 "🐱" [catbin]]
    'find-string-binary = e.id
)(
    catchar: #"🐱"
    did parse3 "🐱" [catchar]
)

[
    (
        bincat: to-binary {C😺T}
        bincat = #{43F09F98BA54}
    )

    (did parse3 bincat [{C😺T}])

    (did parse3 bincat [{c😺t}])

    (didn't parse3/case bincat [{c😺t} <end>])
]

(
    test: to-binary {The C😺T Test}
    did all [
        did parse3 test [to {c😺t} copy x to space to <end>]
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)


(did all [
    did parse3 text: "a ^/ " [
        while [newline remove [to <end>] | "a" [remove [to newline]] | skip]
    ]
    text = "a^/"
])

; FURTHER can be used to detect when parse has stopped advancing the input and
; then not count the rule as a match.
;
[
    (did parse3 "" [while further [to <end>]])

    (didn't parse3 "" [further [opt "a" opt "b"] ("at least one")])
    (did parse3 "a" [further [opt "a" opt "b"] ("at least 1")])
    (did parse3 "a" [further [opt "a" opt "b"] ("at least 1")])
    (did parse3 "ab" [further [opt "a" opt "b"] ("at least 1")])
]

[https://github.com/metaeducation/ren-c/issues/1032 (
    s: {abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ}
    t: {----------------------------------------------------}
    cfor n 2 50 1 [
        sub: copy/part s n
        parse3 sub [while [
            remove skip
            insert ("-")
        ]]
        if sub != copy/part t n [fail "Incorrect Replacement"]
    ]
    true
)]

[(
    countify: func [things data] [
        let counts: make map! []
        let rules: collect [
            for-each t things [
                counts.(t): 0
                keep ^t
                keep ^ compose/deep '(counts.(t): me + 1)
                keep/line [|]
            ]
            keep [fail]
        ]
        parse3 data (compose/deep [
            while [((rules))]  ; could be just `while [rules]`, but it's a test
        ]) then [
            collect [
                for-each [key value] counts [
                    keep ^key
                    keep ^value
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
    (didn't parse3 "aa" [some [#"a"] reject])
    (didn't parse3 "aabb" [some [#"a"] reject some [#"b"]])
    (didn't parse3 "aabb" [some [#"a" reject] to <end>])
]

; Ren-C does not mandate that rules make progress, so matching empty strings
; works, as it does in Red.
[
    (did parse3 "ab" [to [""] "ab"])
    (did parse3 "ab" [to ["a"] "ab"])
    (did parse3 "ab" [to ["ab"] "ab"])
    (did parse3 "ab" [thru [""] "ab"])
    (did parse3 "ab" [thru ["a"] "b"])
    (did parse3 "ab" [thru ["ab"] ""])
]

; Ren-C made it possible to use quoted WORD!s in place of CHAR! or TEXT! to
; match in strings.  This gives a cleaner look, as you drop off 3 vertical
; tick marks from everything like ["ab"] to become just ['ab]
;
(did all [
    pos: parse* "abbbbbc" ['a some ['b], <here>]
    "c" = pos
])
(did all [
    pos: parse* "abbbbc" ['ab, some ['bc | 'b], <here>]
    "" = pos
])
(did all [
    pos: parse* "abc10def" ['abc '10, <here>]
    "def" = pos
])

(
    byteset: make bitset! [0 16 32]
    did parse3 #{001020} [some byteset]
)

; A SET of zero elements gives NULL, a SET of > 1 elements is an error
[(
    x: <before>
    did all [
        did parse3 [1] [set x opt text! integer!]
        x = null
    ]
)(
    x: <before>
    did all [
        did parse3 ["a" 1] [set x some text! integer!]
        x = "a"
    ]
)(
    x: <before>
    e: trap [
        did parse3 ["a" "b" 1] [set x some text! integer!]
    ]
    did all [
        e.id = 'parse-multiple-set
        x = <before>
    ]
)]

[#1251
    (did all [
        did parse3 e: "a" [remove skip insert ("xxx")]
        e = "xxx"
    ])
    (did all [
        did parse3 e: "a" [[remove skip] insert ("xxx")]
        e = "xxx"
    ])
]

[#1245
    (did all [
        did parse3 s: "(1)" [change "(1)" ("()")]
        s = "()"
    ])
]

[#1244
    (did all [
        didn't parse3 a: "12" [remove copy v skip]
        a = "2"
        v = "1"
    ])
    (did all [
        didn't parse3 a: "12" [remove [copy v skip]]
        a = "2"
        v = "1"
    ])
]

[#1298 (
    cset: charset [#"^(01)" - #"^(FF)"]
    did parse3 "a" ["a" while cset]
)(
    cset: charset [# - #"^(FE)"]
    did parse3 "a" ["a" while cset]
)(
    cset: charset [# - #"^(FF)"]
    did parse3 "a" ["a" while cset]
)]

[#1282
    (did parse3 [1 2 a] [thru word!])
]

; Compatibility PARSE2 allows things like set-word and get-word for mark and
; seek funcitonality, uses plain END and not <end>, etc.
;
(did parse2 "aaa" [pos: some "a" to end :pos 3 "a"])

; Parsing URL!s and ANY-SEQUENCE! is read-only
[(
    did all [
        did parse3 http://example.com ["http:" some "/" copy name to "." ".com"]
        name = "example"
    ]
)(
    did all [
        did parse3 'abc.<def>.<ghi>.jkl [word! copy tags some tag! word!]
        tags = [<def> <ghi>]
    ]
)]

; MAYBE is like OPT but if used with SET it will not set the variable if there
; is no match.  The UPARSE implementation is better, but it's mostly just
; added to PARSE3 in case people don't like replacing their ANY and WHILE
; with OPT SOME and find MAYBE SOME more palatable.
[
    (did all [
        x: 10
        did parse3 "" [
            set x maybe "a"
        ]
        x = 10
    ])
    (did all [
        x: 10
        did parse3 "a" [
            set x maybe "a"
        ]
        x = #a
    ])
]
