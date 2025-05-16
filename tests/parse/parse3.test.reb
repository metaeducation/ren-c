; Is PARSE working at all?

(
    parse3 "abc" ["abc"]
    ok
)
(
    parse3 "abc" ["abc" <end>]
    ok
)

; voids match and don't advance input (nulls are errors)
;
(
    parse3 [] [void]
    ok
)
(
    let pos: ~
    parse3 [a b] ['a void pos: <here> 'b]
    pos = [b]
)
(
    parse3 "a" [void "a"]
    ok
)
(
    parse3 "a" [to void "a"]
    ok
)
(
    parse3 "a" [thru void "a"]
    ok
)


; Edge case of matching <END> with TO or THRU
;
(
    parse3 "" [to ["a" | <end>]]
    ok
)
(
    parse3 "" [thru ["a" | <end>]]
    ok
)
(
    parse3 [] [to ["a" | <end>]]
    ok
)
(
    parse3 [] [thru ["a" | <end>]]
    ok
)


[#206 (
    any-char: complement charset ""
    count-up 'n 512 [
        if n = 1 [continue]

        let c
        parse3 (append copy "" make-char n - 1) [
            c: any-char <end>
        ]
        if c != make-char n - 1 [panic "Char didn't match"]
    ]
    ok
)]

(
    var: 3
    rule: "a"
    parse3 "aaa" [repeat (var) rule]  ; clearer than [var rule]
    ok
)

; Don't leak internal detail that BLOB! or ANY-STRING? are 0-terminated
[
    (NUL = as rune! 0)

    ~parse3-incomplete~ !! (parse3 "" [to NUL])
    ~parse3-incomplete~ !! (parse3 "" [thru NUL])
    ~parse3-incomplete~ !! (parse3 "" [to [NUL]])
    ~parse3-incomplete~ !! (parse3 "" [thru [NUL]])

    ~parse3-incomplete~ !! (parse3 #{} [to NUL])
    ~parse3-incomplete~ !! (parse3 #{} [thru NUL])
    ~parse3-incomplete~ !! (parse3 #{} [to [NUL]])
    ~parse3-incomplete~ !! (parse3 #{} [thru [NUL]])
]


[
    ~bad-word-get~ !! (
        foo: ~<bad>~
        parse3 "a" [foo]
        ok
    )
    ~???~ !! (
        foo: '~<bad>~
        parse3 [~<bad>~] [foo <end>]
        ok
    )
]

; Empty block case handling

(
    parse3 [] []
    ok
)
(
    parse3 [] [[[]]]
    ok
)
~parse3-incomplete~ !! (
    parse3 [x] []
)
~parse3-incomplete~ !! (
    parse3 [x] [[[]]]
)
(
    parse3 [x] [[] 'x []]
    ok
)

; No longer contentious concept: NULL is not legal as a parse rule.
; No longer contentious concept: _ is matched literally (it's *the* SPACE char)
[
    ~bad-null~ !! (
        parse3 [x] ['x null]
    )
    (
        parse3 [_ x] [space 'x <end>]
        ok
    )

    ~parse3-incomplete~ !! (
        parse3 [] [space space space]
    )
    (
        parse3 [_ _ _] [space space space]
        ok
    )
    (
        parse3 [_ _ _] [_ _ _]
        ok
    )
    ~parse3-incomplete~ !!(
        parse3 [x <y> "z"] [_ _ _]
    )

    ~parse3-incomplete~ !!(
        parse3 [x <y> "z"] ['_ '_ '_]
    )
    (
        parse3 [_ _ _] ['_ '_ '_]
        ok
    )
    (
        q-space: quote '_
        parse3 [_ _ _] [q-space q-space q-space]
        ok
    )

    ~parse3-incomplete~ !!(
        parse3 [] [[[_ _ _]]]
    )
    (
        parse3 [_ _ _] [[[space space space]]]
        ok
    )
]

; SET-WORD! (store current input position)

(
    let pos
    parse3 ser: [x y] [pos: <here>, one, one]
    pos = ser
)
(
    let pos
    parse3 ser: [x y] [one, pos: <here>, one]
    pos = next ser
)
(
    let pos
    parse3 ser: [x y] [one, one, pos: <here>]
    pos = tail of ser
)
[#2130 (
    val: pos: ~
    parse3 ser: [x] [pos: <here>, val: word!]
    all [val = 'x, pos = ser]
)]
[#2130 (
    val: pos: ~
    parse3 ser: [x] [pos: <here>, val: word!]
    all [val = 'x, pos = ser]
)]
[#2130 (
    val: pos: ~
    res: parse3:match ser: "foo" [pos: <here>, val: across one]
    all [res = null, val = "f", pos = ser]
)]
[#2130 (
    val: pos: ~
    res: parse3:match ser: "foo" [pos: <here>, val: across one]
    all [res = null, val = "f", pos = ser]
)]

; SEEK INTEGER! (replaces TO/THRU integer!

(
    parse3 "abcd" [seek 3 "cd"]
    ok
)
(
    parse3 "abcd" [seek 5]
    ok
)
(
    parse3 "abcd" [seek 128]
    ok
)

[#1965
    (
        parse3 "abcd" [seek 3 one "d"]
        ok
    )
    (
        parse3 "abcd" [seek 4 one]
        ok
    )
    (
        parse3 "abcd" [seek 128]
        ok
    )
    (
        parse3 "abcd" ["ab" seek 1 "abcd"]
        ok
    )
    (
        parse3 "abcd" ["ab" seek 1 one "bcd"]
        ok
    )
]

; parse THRU tag!

[#682 (
    t: null
    parse3 "<tag>text</tag>" [thru '<tag> t: across to '</tag> '</tag>]
    t = "text"
)]

; THRU advances the input position correctly.

(
    i: 0
    j: ~
    parse3 "a" [
        some [thru "a" (i: i + 1, j: if i > 1 [<end> one]) j]
    ]
    i = 1
)

[#1959
    (
        parse3 "abcd" [thru "d"]
        ok
    )
]
[#1959
    (
        parse3 "abcd" [to "d" one]
        ok
    )
]

[#1959
    (
        parse3 "<abcd>" [thru '<abcd>]
        ok
    )
]
[#1959
    (
        parse3 [a b c d] [thru 'd]
        ok
    )
]
[#1959
    (
        parse3 [a b c d] [to 'd one]
        ok
    )
]

; self-invoking rule

;[#1672
;    ~stack-overflow~ !! (
;        a: [a]
;        parse3 [] a
;    )
;]

; repetition

[#1280 (
    i: ~
    parse3 "" [(i: 0) repeat 3 [["a" |] (i: i + 1)]]
    i = 3
)]
[#1268 (
    i: 0
    <infinite?> = catch [
        parse3 "a" [some [(i: i + 1, if i > 100 [throw <infinite?>])]]
    ]
)]
[#1268 (
    i: 0
    j: ~
    parse3 "a" [some [opt "a" (i: i + 1, j: if i = 2 [[veto]]) j]]
    i = 2
)]

; NOT rule

[#1246
    (
        parse3 "1" [not ahead not ahead "1" "1"]
        ok
    )
    (
        parse3 "1" [not ahead [not ahead "1"] "1"]
        ok
    )
    ~parse3-incomplete~ !! (
        parse3 "" [not ahead repeat 0 "a"]
    )
    ~parse3-incomplete~ !! (
        parse3 "" [not ahead [repeat 0 "a"]]
    )
]

[#1240
    (
        parse3 "" [not ahead "a"]
        ok
    )
    (
        parse3 "" [not ahead one]
        ok
    )
    (
        parse3 "" [not ahead veto]
        ok
    )
]


; TO/THRU + bitset!/charset!

[#1457
    (
        parse3 "a" compose [thru (charset "a")]
        ok
    )
    ~parse3-incomplete~ !! (
        parse3 "a" compose [thru (charset "a") one]
    )
    (
        parse3 "ba" compose [to (charset "a") one]
        ok
    )
    ~parse3-incomplete~ !! (
        parse3 "ba" compose [to (charset "a") "ba"]
    )
]

[#2141 (
    xset: charset "x"
    parse3 "x" [thru [xset]]
    ok
)]

; self-modifying rule, not legal in Ren-C if it's during the parse

~series-held~ !! (
    parse3 "abcd" rule: ["ab" (remove back tail of rule) "cd"]
)

[https://github.com/metaeducation/ren-c/issues/377 (
    o: make object! [a: 1]
    parse3 s: "a" [o.a: <here>, one]
    o.a = s
)]

(
    parse3 ["aa"] [ahead text! into ["a" "a"]]
    ok
)

[#1238
    ~parse3-incomplete~ !! (parse3 "ab" [ahead "ab" "ac"])
    ~parse3-incomplete~ !! (parse3 "ac" [ahead "ab" "ac"])
]

; INTO is not legal if a string parse is already running
;
~parse3-rule~ !! (parse3 "aa" [into ["a" "a"]])


; Should return the same series type as input (Rebol2 did not do this)
; PATH! cannot be PARSE'd due to restrictions of the implementation
(
    a-value: first [a/b]
    b-value: ~
    'true = parse3 as block! a-value [b-value: <here>, accept ('true)]
    a-value = as path! b-value
)
(
    a-value: first [()]
    b-value: ~
    'true = parse3 a-value [b-value: <here>, accept ('true)]
    same? a-value b-value
)

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    j: ~
    all [
        error? parse3 "a" [
            some [opt "a" (i: i + 1 j: if i = 2 [[<end> one]]) j]
        ]
        i = 2
    ]
)


; GET-GROUP!
; These evaluate and inject their material into the PARSE, if it is not null.
; They act like a COMPOSE that runs each time the GET-GROUP! is passed.

(
    parse3 "aaabbb" [:([some "a"]) :([some "b"])]
    ok
)
(
    parse3 "aaabbb" [:([some "a"]) :(if null [some "c"]) :([some "b"])]
    ok
)
(
    parse3 "aaa" [:('some) "a"]
    ok
)
~parse3-incomplete~ !! (
    parse3 "aaa" [repeat (1 + 1) "a"]
)
(
    parse3 "aaa" [repeat (1 + 2) "a"]
    ok
)
(
    count: 0
    parse3 ["a" "aa" "aaa"] [some [into [repeat (count: count + 1) "a"]]]
    ok
)

; LOGIC! BEHAVIOR
; A logic OKAY acts as a no-op, while a NULL is illegal (use OPT to get VOID)

(
    parse3 "ab" ["a" okay "b"]
    ok
)
~bad-null~ !! (
    parse3 "ab" ["a" null "b"]
)
(
    parse3 "ab" ["a" :(1 = 1) "b"]
    ok
)
~???~ !! (
    parse3 "ab" ["a" :(1 = 2) "b"]
)
(
    parse3 "ab" ["a" :(opt 1 = 2) "b"]
    ok
)


; QUOTED! BEHAVIOR
; Support for the new literal types

(
    pos: parse3 [... [a b]] [to '[a b], accept <here>]
    pos = [[a b]]
)
(
    parse3 [... [a b]] [thru '[a b]]
    ok
)
(
    parse3 [1 1 1] [some '1]
    ok
)

; Quote level is not retained by captured content
;
(
    x: ~
    pos: parse3 [''[1 + 2]] [into [x: across to <end>], accept <here>]
    all [
        [] = pos
        x = [1 + 2]
    ]
)

; Limited support for @word
(
    block: [some rule]
    parse3 [[some rule] [some rule]] [repeat 2 @block]
    ok
)
(
    ch: #a
    parse3 "a" [@ch]
    ok
)

; As alternatives to using SET-WORD! to set the parse position and GET-WORD!
; to get the parse position, Ren-C has keywords HERE and SEEK.  HERE has
; precedent in Topaz:
;
; https://github.com/giesse/red-topaz-parse
;
; Unlike R3-Alpha, changing the series being parsed is not allowed.
(
    x: y: z: ~
    parse3 "aabbcc" [
        some "a", x: <here>, some "b", y: <here>
        seek x, z: across to <end>
    ]
    all [
        x = "bbcc"
        y = "cc"
        z = "bbcc"
    ]
)(
    pos: 5
    nums: ~
    parse3 "123456789" [seek pos nums: across to <end>]
    nums = "56789"
)


; Multi-byte characters and strings present a lot of challenges.  There should
; be many more tests and philosophies written up of what the semantics are,
; especially when it comes to BLOB! and ANY-STRING? mixtures.  These tests
; are better than nothing...
[
    (
        catchar: #"🐱"
        parse3 #{F09F90B1} [catchar]
        ok
    )
    (
        cattext: "🐱"
        parse3 #{F09F90B1} [cattext]
        ok
    )
    ~find-string-binary~ !! (
        catbin: #{F09F90B1}
        parse3 "🐱" [catbin]
    )
    (
        catchar: #"🐱"
        parse3 "🐱" [catchar]
        ok
    )
]

[
    (
        bincat: to-blob -[C😺T]-
        bincat = #{43F09F98BA54}
    )

    (
        parse3 bincat [-[C😺T]-]
        ok
    )

    (
        parse3 bincat [-[c😺t]-]
        ok
    )

    ~parse3-incomplete~ !! (
        parse3:case bincat [-[c😺t]-]
    )
]

(
    test: to-blob -[The C😺T Test]-
    x: ~
    parse3 test [to -[c😺t]- x: across to space to <end>]
    all [
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)


(
    parse3 text: "a ^/ " [
        some [newline remove [to <end>] | "a" [remove [to newline]] | one]
    ]
    text = "a^/"
)

; FURTHER can be used to detect when parse has stopped advancing the input and
; then not count the rule as a match.
;
[
    (
        parse3 "" [opt some further [to <end>]]
        ok
    )

    ~parse3-incomplete~ !! (
        parse3 "" [further [opt "a" opt "b"] ("at least one")]
    )
    (
        parse3 "a" [further [opt "a" opt "b"] ("at least 1")]
        ok
    )
    (
        parse3 "a" [further [opt "a" opt "b"] ("at least 1")]
        ok
    )
    (
        parse3 "ab" [further [opt "a" opt "b"] ("at least 1")]
        ok
    )
]

[https://github.com/metaeducation/ren-c/issues/1032 (
    s: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    t: "----------------------------------------------------"
    cfor 'n 2 50 1 [
        let sub: copy:part s n
        parse3 sub [some [
            remove one
            insert ("-")
        ]]
        if sub != copy:part t n [panic "Incorrect Replacement"]
    ]
    ok
)]

[(
    countify: func [things data] [
        let counts: to map! []
        let rules: collect [
            for-each 't things [
                counts.(t): 0
                keep t
                keep compose:deep $(counts.(t): me + 1)
                keep:line '|
            ]
            keep 'veto
        ]
        parse3 data (compose:deep [
            opt some [(spread rules)]  ; could also be `opt some [rules]`
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
    ok
)(
    ["a" 3 "b" 3 "c" 3] = countify ["a" "b" "c"] "aaabccbbc"
)(
    <outlier> = countify ["a" "b" "c"] "aaabccbbcd"
)]

[
    https://github.com/rebol/rebol-issues/issues/2393

    ~parse3-incomplete~ !! (
        parse3 "aa" [some [#"a"] reject]
    )
    ~parse3-incomplete~ !! (
        parse3 "aabb" [some [#"a"] reject some [#"b"]]
    )
    ~parse3-incomplete~ !! (
        parse3 "aabb" [some [#"a" reject] to <end>]
    )
]

; Ren-C does not mandate that rules make progress, so matching empty strings
; works, as it does in Red.
[
    (
        parse3 "ab" [to [""] "ab"]
        ok
    )
    (
        parse3 "ab" [to ["a"] "ab"]
        ok
    )
    (
        parse3 "ab" [to ["ab"] "ab"]
        ok
    )
    (
        parse3 "ab" [thru [""] "ab"]
        ok
    )
    (
        parse3 "ab" [thru ["a"] "b"]
        ok
    )
    (
        parse3 "ab" [thru ["ab"] ""]
        ok
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
    ok
)

; A SET of zero elements gives NULL, a SET of > 1 elements is an error
[(
    x: <before>
    parse3 [1] [x: try text! integer!]
    x = null
)(
    x: <before>
    parse3 ["a" 1] [x: some text! integer!]
    x = "a"
)(
    x: <before>
    e: sys.util/rescue [
        parse3 ["a" "b" 1] [x: some text! integer!]
    ]
    all [
        e.id = 'parse3-multi-set
        x = <before>
    ]
)]

[#1251
    (
        parse3 e: "a" [remove one insert ("xxx")]
        e = "xxx"
    )
    (
        parse3 e: "a" [[remove one] insert ("xxx")]
        e = "xxx"
    )
]

[#1245
    (
        parse3 s: "(1)" [change "(1)" ("()")]
        s = "()"
    )
]

[
    (
        parse3 s: ">" [change '> ("greater")]  ; > is WORD!
        s = "greater"
    )
]

[#1244
    (all [
        let [a v]
        error? parse3 a: "12" [remove v: across one]
        a = "2"
        v = "1"
    ])
    (all [
        let [a v]
        error? parse3 a: "12" [remove [v: across one]]
        a = "2"
        v = "1"
    ])
]

[#1298 (
    cset: charset [#"^(01)" - #"^(FF)"]
    parse3 "a" ["a" opt some cset]
    ok
)(
    cset: charset [# - #"^(FE)"]
    parse3 "a" ["a" opt some cset]
    ok
)(
    cset: charset [# - #"^(FF)"]
    parse3 "a" ["a" opt some cset]
    ok
)]

[#1282
    (
        parse3 [1 2 a] [thru word!]
        ok
    )
]

~parse3-multi-set~ !! (parse3 "aaa" [pos: some "a"])

; Parsing URL!s and ANY-SEQUENCE? is read-only
[(
    name: ~
    parse3 http://example.com ["http:" some "/" name: across to "." ".com"]
    name = "example"
)(
    tags: ~
    parse3 'abc.<def>.<ghi>.jkl [word! tags: across some tag! word!]
    tags = [<def> <ghi>]
)]

; The idea of being able to return a value from a parse is implemented via
; the ACCEPT combinator in UPARSE.  This was added to PARSE3.
(
    30 = parse3 "aaa" [some "a" accept (10 + 20)]
)
