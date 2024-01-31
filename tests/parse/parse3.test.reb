; Is PARSE working at all?

(
    parse3 "abc" ["abc"]
    true
)
(
    parse3 "abc" ["abc" <end>]
    true
)

; voids match and don't advance input (nulls are errors)
;
(
    parse3 [] [void]
    true
)
(
    parse3 [a b] ['a void pos: <here> 'b]
    pos = [b]
)
(
    parse3 "a" [void "a"]
    true
)
(
    parse3 "a" [to void "a"]
    true
)
(
    parse3 "a" [thru void "a"]
    true
)


; Edge case of matching <END> with TO or THRU
;
(
    parse3 "" [to ["a" | <end>]]
    true
)
(
    parse3 "" [thru ["a" | <end>]]
    true
)
(
    parse3 [] [to ["a" | <end>]]
    true
)
(
    parse3 [] [thru ["a" | <end>]]
    true
)


[#206 (
    any-char: complement charset ""
    count-up n 512 [
        if n = 1 [continue]

        parse3 (append copy "" codepoint-to-char n - 1) [
            set c any-char <end>
        ]
        if c != codepoint-to-char n - 1 [fail "Char didn't match"]
    ]
    true
)]

(
    var: 3
    rule: "a"
    parse3 "aaa" [repeat (var) rule]  ; clearer than [var rule]
    true
)

; Don't leak internal detail that BINARY! or ANY-STRING! are 0-terminated
[
    (NUL = as issue! 0)

    ~parse-incomplete~ !! (parse3 "" [to NUL])
    ~parse-incomplete~ !! (parse3 "" [thru NUL])
    ~parse-incomplete~ !! (parse3 "" [to [NUL]])
    ~parse-incomplete~ !! (parse3 "" [thru [NUL]])

    ~parse-incomplete~ !! (parse3 #{} [to NUL])
    ~parse-incomplete~ !! (parse3 #{} [thru NUL])
    ~parse-incomplete~ !! (parse3 #{} [to [NUL]])
    ~parse-incomplete~ !! (parse3 #{} [thru [NUL]])
]


; WORD! isotopes cause an error, quasi-word cannot be used as rule
[
    ~bad-word-get~ !! (
        foo: ~bad~
        parse3 "a" [foo]
        true
    )
    ~???~ !! (
        foo: '~bad~
        parse3 [~bad~] [foo <end>]
        true
    )
]

; Empty block case handling

(
    parse3 [] []
    true
)
(
    parse3 [] [[[]]]
    true
)
~parse-incomplete~ !! (
    parse3 [x] []
)
~parse-incomplete~ !! (
    parse3 [x] [[[]]]
)
(
    parse3 [x] [[] 'x []]
    true
)

; No longer contentious concept: NULL is not legal as a parse rule.
;
; BLANK! behavior is still contentious at time of writing.
[
    ~bad-word-get~ !! (
        parse3 [x] ['x null]
    )
    (
        parse3 [_ x] [blank 'x <end>]
        true
    )

    ~parse-incomplete~ !! (
        parse3 [] [blank blank blank]
    )
    (
        parse3 [_ _ _] [blank blank blank]
        true
    )
    (
        parse3 [_ _ _] [_ _ _]
        true
    )
    ~parse-incomplete~ !!(
        parse3 [x <y> "z"] [_ _ _]
    )

    ~parse-incomplete~ !!(
        parse3 [x <y> "z"] ['_ '_ '_]
    )
    (
        parse3 [_ _ _] ['_ '_ '_]
        true
    )
    (
        q-blank: quote '_
        parse3 [_ _ _] [q-blank q-blank q-blank]
        true
    )

    ~parse-incomplete~ !!(
        parse3 [] [[[_ _ _]]]
    )
    (
        parse3 [_ _ _] [[[blank blank blank]]]
        true
    )
]

; SET-WORD! (store current input position)

(
    parse3 ser: [x y] [pos: <here>, skip, skip]
    pos = ser
)
(
    parse3 ser: [x y] [skip, pos: <here>, skip]
    pos = next ser
)
(
    parse3 ser: [x y] [skip, skip, pos: <here>]
    pos = tail of ser
)
[#2130 (
    parse3 ser: [x] [pos: <here>, set val word!]
    all [val = 'x, pos = ser]
)]
[#2130 (
    parse3 ser: [x] [pos: <here>, set val: word!]
    all [val = 'x, pos = ser]
)]
[#2130 (
    res: try parse3 ser: "foo" [pos: <here>, copy val skip]
    all [res = null, val = "f", pos = ser]
)]
[#2130 (
    res: try parse3 ser: "foo" [pos: <here>, copy val: skip]
    all [res = null, val = "f", pos = ser]
)]

; SEEK INTEGER! (replaces TO/THRU integer!

(
    parse3 "abcd" [seek 3 "cd"]
    true
)
(
    parse3 "abcd" [seek 5]
    true
)
(
    parse3 "abcd" [seek 128]
    true
)

[#1965
    (
        parse3 "abcd" [seek 3 skip "d"]
        true
    )
    (
        parse3 "abcd" [seek 4 skip]
        true
    )
    (
        parse3 "abcd" [seek 128]
        true
    )
    (
        parse3 "abcd" ["ab" seek 1 "abcd"]
        true
    )
    (
        parse3 "abcd" ["ab" seek 1 skip "bcd"]
        true
    )
]

; parse THRU tag!

[#682 (
    t: null
    parse3 "<tag>text</tag>" [thru '<tag> copy t to '</tag> '</tag>]
    t == "text"
)]

; THRU advances the input position correctly.

(
    i: 0
    parse3 "a" [
        some [thru "a" (i: i + 1, j: if i > 1 [<end> skip]) j]
    ]
    i == 1
)

[#1959
    (
        parse3 "abcd" [thru "d"]
        true
    )
]
[#1959
    (
        parse3 "abcd" [to "d" skip]
        true
    )
]

[#1959
    (
        parse3 "<abcd>" [thru '<abcd>]
        true
    )
]
[#1959
    (
        parse3 [a b c d] [thru 'd]
        true
    )
]
[#1959
    (
        parse3 [a b c d] [to 'd skip]
        true
    )
]

; self-invoking rule

[#1672
    ~stack-overflow~ !! (
        a: [a]
        parse3 [] a
    )
]

; repetition

[#1280 (
    parse3 "" [(i: 0) 3 [["a" |] (i: i + 1)]]
    i == 3
)]
[#1268 (
    i: 0
    <infinite?> = catch [
        parse3 "a" [some [(i: i + 1, if i > 100 [throw <infinite?>])]]
    ]
)]
[#1268 (
    i: 0
    parse3 "a" [some [try "a" (i: i + 1, j: if i = 2 [[fail]]) j]]
    i == 2
)]

; NOT rule

[#1246
    (
        parse3 "1" [not not "1" "1"]
        true
    )
    (
        parse3 "1" [not [not "1"] "1"]
        true
    )
    ~parse-incomplete~ !! (
        parse3 "" [not 0 "a"]
    )
    ~parse-incomplete~ !! (
        parse3 "" [not [0 "a"]]
    )
]

[#1240
    (
        parse3 "" [not "a"]
        true
    )
    (
        parse3 "" [not skip]
        true
    )
    (
        parse3 "" [not fail]
        true
    )
]


; TO/THRU + bitset!/charset!

[#1457
    (
        parse3 "a" compose [thru (charset "a")]
        true
    )
    ~parse-incomplete~ !! (
        parse3 "a" compose [thru (charset "a") skip]
    )
    (
        parse3 "ba" compose [to (charset "a") skip]
        true
    )
    ~parse-incomplete~ !! (
        parse3 "ba" compose [to (charset "a") "ba"]
    )
]

[#2141 (
    xset: charset "x"
    parse3 "x" [thru [xset]]
    true
)]

; self-modifying rule, not legal in Ren-C if it's during the parse

~series-held~ !! (
    parse3 "abcd" rule: ["ab" (remove back tail of rule) "cd"]
)

[https://github.com/metaeducation/ren-c/issues/377 (
    o: make object! [a: 1]
    parse3 s: "a" [o.a: <here>, skip]
    o.a = s
)]

; AHEAD and AND are synonyms
;
(
    parse3 ["aa"] [ahead text! into ["a" "a"]]
    true
)
(
    parse3 ["aa"] [and text! into ["a" "a"]]
    true
)

[#1238
    ~parse-incomplete~ !! (parse3 "ab" [ahead "ab" "ac"])
    ~parse-incomplete~ !! (parse3 "ac" [ahead "ab" "ac"])
]

; INTO is not legal if a string parse is already running
;
~parse-rule~ !! (parse3 "aa" [into ["a" "a"]])


; Should return the same series type as input (Rebol2 did not do this)
; PATH! cannot be PARSE'd due to restrictions of the implementation
(
    a-value: first [a/b]
    parse3 as block! a-value [b-value: <here>, accept (true)]
    a-value = to path! b-value
)
(
    a-value: first [()]
    parse3 a-value [b-value: <here>, accept (true)]
    same? a-value b-value
)

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    did all [
        raised? parse3 "a" [
            some [try "a" (i: i + 1 j: if i = 2 [[<end> skip]]) j]
        ]
        i == 2
    ]
)


; GET-GROUP!
; These evaluate and inject their material into the PARSE, if it is not null.
; They act like a COMPOSE that runs each time the GET-GROUP! is passed.

(
    parse3 "aaabbb" [:([some "a"]) :([some "b"])]
    true
)
(
    parse3 "aaabbb" [:([some "a"]) :(if false [some "c"]) :([some "b"])]
    true
)
(
    parse3 "aaa" [:('some) "a"]
    true
)
~parse-incomplete~ !! (
    parse3 "aaa" [:(1 + 1) "a"]
)
(
    parse3 "aaa" [:(1 + 2) "a"]
    true
)
(
    count: 0
    parse3 ["a" "aa" "aaa"] [some [into [:(count: count + 1) "a"]]]
    true
)

; LOGIC! BEHAVIOR
; A logic true acts as a no-op, while a logic false causes matches to fail

(
    parse3 "ab" ["a" true "b"]
    true
)
~parse-incomplete~ !! (
    parse3 "ab" ["a" false "b"]
)
(
    parse3 "ab" ["a" :(1 = 1) "b"]
    true
)
~parse-incomplete~ !! (
    parse3 "ab" ["a" :(1 = 2) "b"]
)


; QUOTED! BEHAVIOR
; Support for the new literal types

(
    pos: parse3 [... [a b]] [to '[a b], accept <here>]
    pos = [[a b]]
)
(
    parse3 [... [a b]] [thru '[a b]]
    true
)
(
    parse3 [1 1 1] [some '1]
    true
)

; Quote level is not retained by captured content
;
(
    pos: parse3 [''[1 + 2]] [into [copy x to <end>], accept <here>]
    did all [
        [] == pos
        x == [1 + 2]
    ]
)

; Limited support for @word
(
    block: [some rule]
    parse3 [[some rule] [some rule]] [2 @block]
    true
)
(
    ch: #a
    parse3 "a" [@ch]
    true
)

; As alternatives to using SET-WORD! to set the parse position and GET-WORD!
; to get the parse position, Ren-C has keywords HERE and SEEK.  HERE has
; precedent in Topaz:
;
; https://github.com/giesse/red-topaz-parse
;
; Unlike R3-Alpha, changing the series being parsed is not allowed.
(
    parse3 "aabbcc" [
        some "a", x: <here>, some "b", y: <here>
        seek x, copy z to <end>
    ]
    did all [
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
[
    (
        catchar: #"🐱"
        parse3 #{F09F90B1} [catchar]
        true
    )
    (
        cattext: "🐱"
        parse3 #{F09F90B1} [cattext]
        true
    )
    ~find-string-binary~ !! (
        catbin: #{F09F90B1}
        parse3 "🐱" [catbin]
        true
    )
    (
        catchar: #"🐱"
        parse3 "🐱" [catchar]
        true
    )
]

[
    (
        bincat: to-binary {C😺T}
        bincat = #{43F09F98BA54}
    )

    (
        parse3 bincat [{C😺T}]
        true
    )

    (
        parse3 bincat [{c😺t}]
        true
    )

    ~parse-incomplete~ !! (
        parse3/case bincat [{c😺t}]
    )
]

(
    test: to-binary {The C😺T Test}
    parse3 test [to {c😺t} copy x to space to <end>]
    did all [
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)


(
    parse3 text: "a ^/ " [
        some [newline remove [to <end>] | "a" [remove [to newline]] | skip]
    ]
    text = "a^/"
)

; FURTHER can be used to detect when parse has stopped advancing the input and
; then not count the rule as a match.
;
[
    (
        parse3 "" [try some further [to <end>]]
        true
    )

    ~parse-incomplete~ !! (
        parse3 "" [further [try "a" try "b"] ("at least one")]
    )
    (
        parse3 "a" [further [try "a" try "b"] ("at least 1")]
        true
    )
    (
        parse3 "a" [further [try "a" try "b"] ("at least 1")]
        true
    )
    (
        parse3 "ab" [further [try "a" try "b"] ("at least 1")]
        true
    )
]

[https://github.com/metaeducation/ren-c/issues/1032 (
    s: {abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ}
    t: {----------------------------------------------------}
    cfor n 2 50 1 [
        sub: copy/part s n
        parse3 sub [some [
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
                keep t
                keep compose/deep @(counts.(t): me + 1)
                keep/line '|
            ]
            keep 'fail
        ]
        parse3 data (compose/deep [
            try some [(spread rules)]  ; could also be `try some [rules]`
        ]) except [
            return <outlier>
        ]
        return collect [
            for-each [key value] counts [
                keep key
                keep value
            ]
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

    ~parse-incomplete~ !! (
        parse3 "aa" [some [#"a"] reject]
    )
    ~parse-incomplete~ !! (
        parse3 "aabb" [some [#"a"] reject some [#"b"]]
    )
    ~parse-incomplete~ !! (
        parse3 "aabb" [some [#"a" reject] to <end>]
    )
]

; Ren-C does not mandate that rules make progress, so matching empty strings
; works, as it does in Red.
[
    (
        parse3 "ab" [to [""] "ab"]
        true
    )
    (
        parse3 "ab" [to ["a"] "ab"]
        true
    )
    (
        parse3 "ab" [to ["ab"] "ab"]
        true
    )
    (
        parse3 "ab" [thru [""] "ab"]
        true
    )
    (
        parse3 "ab" [thru ["a"] "b"]
        true
    )
    (
        parse3 "ab" [thru ["ab"] ""]
        true
    )
]

; Ren-C made it possible to use quoted WORD!s in place of CHAR! or TEXT! to
; match in strings.  This gives a cleaner look, as you drop off 3 vertical
; tick marks from everything like ["ab"] to become ['ab]
;
(
    pos: parse3 "abbbbbc" ['a some ['b], accept <here>]
    "c" = pos
)
(
    pos: parse3 "abbbbc" ['ab, some ['bc | 'b], accept <here>]
    "" = pos
)
(
    pos: parse3 "abc10def" ['abc '10, accept <here>]
    "def" = pos
)

(
    byteset: make bitset! [0 16 32]
    parse3 #{001020} [some byteset]
    true
)

; A SET of zero elements gives NULL, a SET of > 1 elements is an error
[(
    x: <before>
    parse3 [1] [set x try text! integer!]
    x = null
)(
    x: <before>
    parse3 ["a" 1] [set x some text! integer!]
    x = "a"
)(
    x: <before>
    e: sys.util.rescue [
        parse3 ["a" "b" 1] [set x some text! integer!]
    ]
    did all [
        e.id = 'parse-multiple-set
        x = <before>
    ]
)]

[#1251
    (
        parse3 e: "a" [remove skip insert ("xxx")]
        e = "xxx"
    )
    (
        parse3 e: "a" [[remove skip] insert ("xxx")]
        e = "xxx"
    )
]

[#1245
    (
        parse3 s: "(1)" [change "(1)" ("()")]
        s = "()"
    )
]

[#1244
    (did all [
        raised? parse3 a: "12" [remove copy v skip]
        a = "2"
        v = "1"
    ])
    (did all [
        raised? parse3 a: "12" [remove [copy v skip]]
        a = "2"
        v = "1"
    ])
]

[#1298 (
    cset: charset [#"^(01)" - #"^(FF)"]
    parse3 "a" ["a" try some cset]
    true
)(
    cset: charset [# - #"^(FE)"]
    parse3 "a" ["a" try some cset]
    true
)(
    cset: charset [# - #"^(FF)"]
    parse3 "a" ["a" try some cset]
    true
)]

[#1282
    (
        parse3 [1 2 a] [thru word!]
        true
    )
]

; Compatibility Redbol PARSE allows things like set-word and get-word for mark
; and seek functionality, uses plain END and not <end>, etc.
;
(did parse3/redbol "aaa" [pos: some "a" to end :pos 3 "a"])

; Parsing URL!s and ANY-SEQUENCE! is read-only
[(
    parse3 http://example.com ["http:" some "/" copy name to "." ".com"]
    name = "example"
)(
    parse3 'abc.<def>.<ghi>.jkl [word! copy tags some tag! word!]
    tags = [<def> <ghi>]
)]

; The idea of being able to return a value from a parse is implemented via
; the ACCEPT combinator in UPARSE.  This was added to PARSE3.
(
    30 = parse "aaa" [some "a" accept (10 + 20)]
)
