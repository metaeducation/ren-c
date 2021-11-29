; Is PARSE working at all?

(parse? "abc" ["abc"])
(parse? "abc" ["abc" <end>])

; Edge case of matching <END> with TO or THRU
;
(parse? "" [to ["a" | <end>]])
(parse? "" [thru ["a" | <end>]])
(parse? [] [to ["a" | <end>]])
(parse? [] [thru ["a" | <end>]])


[#206 (
    any-char: complement charset ""
    count-up n 512 [
        if n = 1 [continue]

        if not parse? (append copy "" make char! n - 1) [set c any-char <end>] [
            fail "Parse didn't work"
        ]
        if c != make char! n - 1 [fail "Char didn't match"]
    ]
    true
)]

(
    var: 3
    rule: "a"
    parse? "aaa" [repeat (var) rule]  ; clearer than [var rule]
)

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
    e/id = 'bad-word-get
)(
    foo: quote '~void~
    parse? [~void~] [foo <end>]
)

; Empty block case handling

(parse? [] [])
(parse? [] [[[]]])
(not parse? [x] [])
(not parse? [x] [[[]]])
(parse? [x] [[] 'x []])

; Literal blank vs. fetched blank/null handling.
; Literal blank means "skip" at source level, but if retrieved from a variable
; it means the same as null.
; https://forum.rebol.info/t/1348
[
    (parse? [x] ['x null])
    (parse? [x] [blank 'x <end>])

    (parse? [] [blank blank blank])
    (not parse? [] [_ _ _])
    (parse? [x <y> "z"] [_ _ _])

    (not parse? [x <y> "z"] ['_ '_ '_])
    (parse? [_ _ _] ['_ '_ '_])
    (
        q-blank: quote _
        parse? [_ _ _] [q-blank q-blank q-blank]
    )

    (not parse? [] [[[_ _ _]]])
    (parse? [] [[[blank blank blank]]])
    (parse? [] [[[null null null]]])
]

; SET-WORD! (store current input position)

(
    res: parse? ser: [x y] [pos: <here>, skip, skip]
    all [res, pos = ser]
)
(
    res: parse? ser: [x y] [skip, pos: <here>, skip]
    all [res, pos = next ser]
)
(
    res: parse? ser: [x y] [skip, skip, pos: <here>]
    all [res, pos = tail of ser]
)
[#2130 (
    res: parse? ser: [x] [pos: <here>, set val word!]
    all [res, val = 'x, pos = ser]
)]
[#2130 (
    res: parse? ser: [x] [pos: <here>, set val: word!]
    all [res, val = 'x, pos = ser]
)]
[#2130 (
    res: parse? ser: "foo" [pos: <here>, copy val skip]
    all [not res, val = "f", pos = ser]
)]
[#2130 (
    res: parse? ser: "foo" [pos: <here>, copy val: skip]
    all [not res, val = "f", pos = ser]
)]

; SEEK INTEGER! (replaces TO/THRU integer!

(parse? "abcd" [seek 3 "cd"])
(parse? "abcd" [seek 5])
(parse? "abcd" [seek 128])

[#1965
    (parse? "abcd" [seek 3 skip "d"])
    (parse? "abcd" [seek 4 skip])
    (parse? "abcd" [seek 128])
    (parse? "abcd" ["ab" seek 1 "abcd"])
    (parse? "abcd" ["ab" seek 1 skip "bcd"])
]

; parse THRU tag!

[#682 (
    t: _
    parse "<tag>text</tag>" [thru '<tag> copy t to '</tag>]
    t == "text"
)]

; THRU advances the input position correctly.

(
    i: 0
    parse "a." [
        while [thru "a" (i: i + 1 j: try if i > 1 [<end> skip]) j]
    ]
    i == 1
)

[#1959
    (parse? "abcd" [thru "d"])
]
[#1959
    (parse? "abcd" [to "d" skip])
]

[#1959
    (parse? "<abcd>" [thru '<abcd>])
]
[#1959
    (parse? [a b c d] [thru 'd])
]
[#1959
    (parse? [a b c d] [to 'd skip])
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
        parse "a" [while [(i: i + 1) (if i > 100 [throw <infinite?>])]]
    ]
)]
[#1268 (
    i: 0
    parse "a" [while [(i: i + 1 j: try if i = 2 [[fail]]) j]]
    i == 2
)]

; NOT rule

[#1246
    (parse? "1" [not not "1" "1"])
]
[#1246
    (parse? "1" [not [not "1"] "1"])
]
[#1246
    (not parse? "" [not 0 "a"])
]
[#1246
    (not parse? "" [not [0 "a"]])
]
[#1240
    (parse? "" [not "a"])
]
[#1240
    (parse? "" [not skip])
]
[#1240
    (parse? "" [not fail])
]


; TO/THRU + bitset!/charset!

[#1457
    (parse? "a" compose [thru (charset "a")])
]
[#1457
    (not parse? "a" compose [thru (charset "a") skip])
]
[#1457
    (parse? "ba" compose [to (charset "a") skip])
]
[#1457
    (not parse? "ba" compose [to (charset "a") "ba"])
]
[#2141 (
    xset: charset "x"
    parse? "x" [thru [xset]]
)]

; self-modifying rule, not legal in Ren-C if it's during the parse

(error? trap [
    not parse? "abcd" rule: ["ab" (remove back tail of rule) "cd"]
])

[https://github.com/metaeducation/ren-c/issues/377 (
    o: make object! [a: 1]
    parse s: "a" [o/a: skip]
    o/a = s
)]

; AHEAD and AND are synonyms
;
(parse? ["aa"] [ahead text! into ["a" "a"]])
(parse? ["aa"] [and text! into ["a" "a"]])

[#1238
    (null = parse "ab" [ahead "ab" "ac"])
    (null = parse "ac" [ahead "ab" "ac"])
]

; INTO is not legal if a string parse is already running
;
(error? trap [parse "aa" [into ["a" "a"]]])


; Should return the same series type as input (Rebol2 did not do this)
; PATH! cannot be PARSE'd due to restrictions of the implementation
(
    a-value: first [a/b]
    parse as block! a-value [b-value: <here>]
    a-value = to path! b-value
)
(
    a-value: first [()]
    parse a-value [b-value: <here>]
    same? a-value b-value
)

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    parse "a" [while [(i: i + 1 j: if i = 2 [[<end> skip]]) j]]
    i == 2
)


; GET-GROUP!
; These evaluate and inject their material into the PARSE, if it is not null.
; They act like a COMPOSE/ONLY that runs each time the GET-GROUP! is passed.

(parse? "aaabbb" [:([some "a"]) :([some "b"])])
(parse? "aaabbb" [:([some "a"]) :(if false [some "c"]) :([some "b"])])
(parse? "aaa" [:('some) "a"])
(not parse? "aaa" [:(1 + 1) "a"])
(parse? "aaa" [:(1 + 2) "a"])
(
    count: 0
    parse? ["a" "aa" "aaa"] [some [into [:(count: count + 1) "a"]]]
)

; SET-GROUP!
; What these might do in PARSE could be more ambitious, but for starters they
; provide a level of indirection in SET.

(
    m: null
    word: 'm
    did all [
        parse? [1020] [(word): integer!]
        word = 'm
        m = 1020
    ]
)

; LOGIC! BEHAVIOR
; A logic true acts as a no-op, while a logic false causes matches to fail

(parse? "ab" ["a" true "b"])
(not parse? "ab" ["a" false "b"])
(parse? "ab" ["a" :(1 = 1) "b"])
(not parse? "ab" ["a" :(1 = 2) "b"])


; QUOTED! BEHAVIOR
; Support for the new literal types

(
    did all [
        pos: parse* [... [a b]] [to '[a b], <here>]
        pos = [[a b]]
    ]
)
(parse? [... [a b]] [thru '[a b]])
(parse? [1 1 1] [some '1])

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
        parse? "aabbcc" [
            some "a", x: <here>, some "b", y: <here>
            seek x, copy z to <end>
        ]
        x = "bbcc"
        y = "cc"
        z = "bbcc"
    ]
)(
    pos: 5
    parse "123456789" [seek pos copy nums to <end>]
    nums = "56789"
)


; Multi-byte characters and strings present a lot of challenges.  There should
; be many more tests and philosophies written up of what the semantics are,
; especially when it comes to BINARY! and ANY-STRING! mixtures.  These tests
; are better than nothing...
(
    catchar: #"🐱"
    parse? #{F09F90B1} [catchar]
)(
    cattext: "🐱"
    parse? #{F09F90B1} [cattext]
)(
    catbin: #{F09F90B1}
    e: trap [parse? "🐱" [catbin]]
    'find-string-binary = e/id
)(
    catchar: #"🐱"
    parse? "🐱" [catchar]
)

[
    (
        bincat: to-binary {C😺T}
        bincat = #{43F09F98BA54}
    )

    (parse? bincat [{C😺T}])

    (parse? bincat [{c😺t}])

    (not parse?/case bincat [{c😺t} <end>])
]

(
    test: to-binary {The C😺T Test}
    did all [
        parse? test [to {c😺t} copy x to space to <end>]
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)


(did all [
    parse? text: "a ^/ " [
        while [newline remove [to <end>] | "a" [remove [to newline]] | skip]
    ]
    text = "a^/"
])

; FURTHER can be used to detect when parse has stopped advancing the input and
; then not count the rule as a match.
;
[
    (parse? "" [while further [to <end>]])

    (not parse? "" [further [opt "a" opt "b"] ("at least one")])
    (parse? "a" [further [opt "a" opt "b"] ("at least 1")])
    (parse? "a" [further [opt "a" opt "b"] ("at least 1")])
    (parse? "ab" [further [opt "a" opt "b"] ("at least 1")])
]

[https://github.com/metaeducation/ren-c/issues/1032 (
    s: {abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ}
    t: {----------------------------------------------------}
    cfor n 2 50 1 [
        sub: copy/part s n
        parse sub [while [
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
                counts/(t): 0
                keep ^t
                keep ^ compose/deep '(counts/(t): me + 1)
                keep/line [|]
            ]
            keep [fail]
        ]
        parse data (compose/deep [
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
    (not parse? "aa" [some [#"a"] reject])
    (not parse? "aabb" [some [#"a"] reject some [#"b"]])
    (not parse? "aabb" [some [#"a" reject] to <end>])
]

; Ren-C does not mandate that rules make progress, so matching empty strings
; works, as it does in Red.
[
    (parse? "ab" [to [""] "ab"])
    (parse? "ab" [to ["a"] "ab"])
    (parse? "ab" [to ["ab"] "ab"])
    (parse? "ab" [thru [""] "ab"])
    (parse? "ab" [thru ["a"] "b"])
    (parse? "ab" [thru ["ab"] ""])
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
    parse? #{001020} [some byteset]
)

; A SET of zero elements gives NULL, a SET of > 1 elements is an error
[(
    x: <before>
    did all [
        parse? [1] [set x opt text! integer!]
        x = null
    ]
)(
    x: <before>
    did all [
        parse? ["a" 1] [set x some text! integer!]
        x = "a"
    ]
)(
    x: <before>
    e: trap [
        parse? ["a" "b" 1] [set x some text! integer!]
    ]
    did all [
        e/id = 'parse-multiple-set
        x = <before>
    ]
)]

[#1251
    (did all [
        parse? e: "a" [remove skip insert ("xxx")]
        e = "xxx"
    ])
    (did all [
        parse? e: "a" [[remove skip] insert ("xxx")]
        e = "xxx"
    ])
]

[#1245
    (did all [
        parse? s: "(1)" [change "(1)" ("()")]
        s = "()"
    ])
]

[#1244
    (did all [
        not parse? a: "12" [remove copy v skip]
        a = "2"
        v = "1"
    ])
    (did all [
        not parse? a: "12" [remove [copy v skip]]
        a = "2"
        v = "1"
    ])
]

[#1298 (
    cset: charset [#"^(01)" - #"^(FF)"]
    parse? "a" ["a" while cset]
)(
    cset: charset [# - #"^(FE)"]
    parse? "a" ["a" while cset]
)(
    cset: charset [# - #"^(FF)"]
    parse? "a" ["a" while cset]
)]

[#1282
    (parse? [1 2 a] [thru word!])
]

; Compatibility PARSE2 allows things like set-word and get-word for mark and
; seek funcitonality, uses plain END and not <end>, etc.
;
(parse2? "aaa" [pos: some "a" to end :pos 3 "a"])

; Parsing URL!s and ANY-SEQUENCE! is read-only
[(
    did all [
        parse? http://example.com ["http:" some "/" copy name to "." ".com"]
        name = "example"
    ]
)(
    did all [
        parse? 'abc.<def>.<ghi>.jkl [word! copy tags some tag! word!]
        tags = [<def> <ghi>]
    ]
)]