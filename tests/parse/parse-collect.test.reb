; %parse-collect.test.reb
;
; COLLECT is implemented on top of a generic facility for storing "pending"
; results.  This gives the impression of "rollback"--though what is actually
; happening is that combinators are bubbling up a list of items that can be
; filtered by other combinators to extract data from.
;
; Most combinators allow the bubbling up to happen automatically--where every
; successful parser they call will contribute its results to the growing array
; in the order the parsers were called.  This is not good enough for some
; combinators (like BLOCK!) which have higher-level concepts than the mere
; success of individual parsers to decide what is kept.  (e.g. a parser must
; be part of an entire successful *alternate group* of parsers to have its
; "pendings" contribute to the result).


[
    ([] = parse [] [collect []])
    ([] = parse "" [collect []])
    ([] = parse #{} [collect []])

    (raised? parse [1] [collect []])
    (raised? parse "1" [collect []])
    (raised? parse #{01} [collect []])

    ([1] = parse [1] [collect [keep <any>]])
    ([#1] = parse "1" [collect [keep <any>]])
    ([1] = parse #{01} [collect [keep <any>]])

    ([1 2 3] = parse [1 2 3] [collect [some [keep integer!]]])
    (
        digit: charset "0123456789"
        [#1 #2 #3] = parse "123" [collect [some [keep digit]]]
    )
    (
        digit: charset [0 - 9]
        [1 2 3] = parse #{010203} [collect [some [keep digit]]]
    )

    (
        digit: charset "0123456789"
        [#2] = parse "123" [
            collect [some [
                keep [v: digit, :(even? load-value as text! v)]
                |
                <any>
            ]]
        ]
    )
    (
        digit: charset [0 - 9]
        [2] = parse #{010203} [
            collect [some [
                keep [v: digit, :(even? v)]
                |
                <any>
            ]]
        ]
    )

    (
        digit: charset "0123456789"
        [1 2 3] = parse "123" [
            collect [some [d: across digit keep (load-value d)]]
        ]
    )
    (
        digit: charset [0 - 9]
        [2 3 4] = parse #{010203} [
            collect [some [d: across digit keep (1 + first d)]]
        ]
    )

    (
        ["aa" "bbb"] = parse "aabbb" [
            collect [keep across some "a", keep across some #b]
        ]
    )
    (
        [#{0A0A} #{0B0B0B}] = parse #{0A0A0B0B0B} [
            collect [keep across some #{0A}, keep across some #{0B}]
        ]
    )

    (
        alpha: charset [#a - #z]
        ["abc" "def"] = parse "abc|def" [
            collect [some [keep across some alpha | <any>]]
        ]
    )
    (
        digit: charset [0 - 9]
        [#{010203} #{040506}] = parse #{01020311040506} [
            collect [some [keep across some digit | <any>]]
        ]
    )
]


; Working with SET-WORD!
[
    (
        a: ~
        did all [
            [] == parse [] [a: collect []]
            a = []
        ]
    )
    (
        a: ~
        did all [
            [] == parse "" [a: collect []]
            a = []
        ]
    )
    (
        a: ~
        did all [
            [] == parse #{} [a: collect []]
            a = []
        ]
    )

    (
        a: ~
        did all [
            [1] == parse [1] [a: collect [keep <any>]]
            a = [1]
        ]
    )
    (
        a: ~
        did all [
            [#1] == parse "1" [a: collect [keep <any>]]
            a = [#1]
        ]
    )
    (
        a: ~
        did all [
            [1] == parse #{01} [a: collect [keep <any>]]
            a = [1]
        ]
    )
]

; As soon as you use the COLLECT keyword in a Red PARSE you get a BLOCK! result
; regardless of overall success or failure.  But UPARSE always returns NULL if
; a rule you asked to have match fails.  There are other tools for getting
; the result out if you wish.
[
    ([1 2 3] = parse [1 2 3 yay] [collect [some keep integer!] elide word!])
    ('yay = parse [1 2 3 yay] [collect [some keep integer!] word!])
    (raised? parse [1 2 3 <bomb>] [collect [some keep integer!] word!])
    ([1 2 3] = parse [1 2 3 <bomb>] [
        collect [some keep integer!] elide to <end>
    ])

    (did all [
        'yay = parse [1 2 3 yay] [block: collect [some keep integer!] word!]
        block = [1 2 3]
    ])
]


[(
    x: ~
    did all [
        [1 2] == parse [1 2] [x: collect [
            keep integer! keep tag! | keep integer! keep integer!
        ]]
        x = [1 2]
    ]
)(
    x: ~
    did all [  ; semi-nonsensical use of BETWEEN just because it takes 2 rules
        ["(" ")"] == parse "(abc)" [x: collect between keep "(" keep ")"]
        x = ["(" ")"]
    ]
)(
    x: <before>
    did all [  ; semi-nonsensical use of BETWEEN just because it takes 2 rules
        raised? parse "(abc}" [x: collect between "(" keep ")"]
        x = <before>
    ]
)(
    x: ~
    did all [
        [#a <kept> #a <kept> #a <kept>] == parse "aaa" [x: collect [some [
            keep (if false [<not kept>])
            keep <any>
            keep (if true [<kept>])
        ]]]
        x = [#a <kept> #a <kept> #a <kept>]
    ]
)]


; Note potential confusion that SOME KEEP and KEEP SOME are not the same.
[
    (["a" "a" "a"] = parse "aaa" [collect [some keep "a"]])
    (["a"] = parse "aaa" [collect [keep some "a"]])
]


; SPREAD can be used to keep the contents of a result rule block, vs. the
; default of keeping the block as-is.
[(
    result: parse "abbbbabbab" [collect [
        some [keep "a", keep spread collect [some keep "b" keep (<hi>)]]
    ]]
    result = ["a" "b" "b" "b" "b" <hi> "a" "b" "b" <hi> "a" "b" <hi>]
)(
    result: parse "abbbbabbab" [collect [
        some [keep "a", keep [collect [some keep "b" keep (<hi>)]]]
    ]]
    result = ["a" ["b" "b" "b" "b" <hi>] "a" ["b" "b" <hi>] "a" ["b" <hi>]]
)]


; You can KEEP inside a KEEP rule.
[
    (["a" "a"] = parse "aaa" [collect [keep keep "a", "aa"]])
    (["a" "a" "a"] = parse "aaa" [collect [keep [keep "a" keep "a"] "a"]])

    (["aaa" "aaa"] = parse "aaa" [
        collect [keep keep "a", "b" | keep keep "aaa"]
    ])
    (["a" "a" "a" "c" "c"] = parse "aaa" [
        collect [keep [keep "a" keep "a"] [keep "b" | keep ["a" keep ("c")]]]
    ])
]


; SOME KEEP vs KEEP SOME
[
    (did all [
        [3] == parse [1 2 3] [x: collect [keep some integer!]]
        x = [3]
    ])
    (did all [
        [1 2 3] == parse [1 2 3] [x: collect [some keep integer!]]
        x = [1 2 3]
    ])
    (did all [
        [3] == parse [1 2 3] [x: collect [keep [some integer!]]]
        x = [3]
    ])
    (did all [
        [1 2 3] == parse [1 2 3] [x: collect [some [keep integer!]]]
        x = [1 2 3]
    ])
]

; Collecting non-array series fragments
[
    (did all [
        pos: parse- "aaabbb" [x: collect [keep [across some "a"]]]
        "bbb" = pos
        x = ["aaa"]
    ])
    (did all [
        pos: parse- "aaabbbccc" [
            x: collect [keep [across some "a"] some "b" keep [across some "c"]]
        ]
        "" = pos
        x = ["aaa" "ccc"]
    ])
]

; "Backtracking" (more tests needed!)
[
    (did all [
        pos: parse- [1 2 3] [
            x: collect [
                keep integer! keep integer! keep text!
                |
                keep integer! keep spread across some integer!
            ]
        ]
        [] = pos
        x = [1 2 3]
    ])
]

; No change to variable on failed match (consistent with Rebol2/R3-Alpha/Red
; behaviors w.r.t SET and COPY)
[
    (did all [
        x: <before>
        raised? parse [1 2] [x: collect [keep integer! keep text!]]
        x = <before>
    ])
]

; Nested collect
[
    (did all [
        [] == parse [1 2 3 4] [
            a: collect [
                keep integer!
                b: collect [keep spread across repeat 2 integer!]
                keep integer!
            ]
            <end>
        ]

        a = [1 4]
        b = [2 3]
    ])
]

; GROUP! can be used to keep material that did not originate from the
; input series or a match rule.
[
    (did all [
        pos: parse- [1 2 3] [
            x: collect [
                keep integer!
                keep spread (second [A [<pick> <me>] B])
                keep integer!
            ]
        ]
        [3] = pos
        x = [1 <pick> <me> 2]
    ])
    (did all [
        pos: parse- [1 2 3] [
            x: collect [
                keep integer!
                keep (second [A [<pick> <me>] B])
                keep integer!
            ]
        ]
        [3] = pos
        x = [1 [<pick> <me>] 2]
    ])
    (did all [
        [[a b c]] == parse [1 2 3] [x: collect [keep ([a b c]) to <end>]]
        x = [[a b c]]
    ])
]

; KEEP without blocks
https://github.com/metaeducation/ren-c/issues/935
[
    (did all [
        ["aaa" "b"] == parse "aaabbb" [
            x: collect [keep across some "a" keep some "b"]
        ]
        x = ["aaa" "b"]
    ])

    (did all [
        "" == parse "aaabbb" [x: collect [keep across to "b"] to <end>]
        x = ["aaa"]
    ])

    (did all [
        ["b"] == parse "aaabbb" [
            outer: collect [
                some [inner: collect keep across some "a" | keep some "b"]
            ]
        ]
        outer = ["b"]
        inner = ["aaa"]
    ])
]

; Filtering tests (from %parse-test.red)
[
    (
        [2] = parse [1 2 3] [
            collect [some [keep [v: integer! :(even? v)] | <any>]]
        ]
    )
    (
        [3 4 8] = parse [a 3 4 t "test" 8] [
            collect [some [keep integer! | <any>]]
        ]
    )
    (
        list: ~
        did all [
            [3 4 8] == parse [a 3 4 t "test" 8] [
                list: collect [some [keep integer! | <any>]]
            ]
            list = [3 4 8]
        ]
    )
]

; Note difference between KEEP ACROSS SOME and KEEP SOME.
[
    (
        [[b b b]] = parse [a b b b] [collect [<any>, keep across some 'b]]
    )
    (
        [b b b] = parse [a b b b] [collect [<any>, keep spread across some 'b]]
    )
    (
        [b] = parse [a b b b] [collect [<any>, keep some 'b]]
    )
]

[https://github.com/red/red/issues/567
    (
        ["12"] = parse "12" [collect [keep value: across repeat 2 <any>]]
    )
]

[https://github.com/red/red/issues/569
    (
        size: 1
        ["1"] = parse "1" [collect [keep value: across repeat (size) <any>]]
    )(
        size: 2
        ["12"] = parse "12" [collect [keep value: across repeat (size) <any>]]
    )
]

; Red does a no-op keep for `keep to end` if you are on the end.  This is
; incongruous with what happens if you set a variable that way:
;
;     red>> parse "" [test: to end]
;     == true
;
;     red>> test
;     == ""
;
[https://github.com/red/red/issues/2561
    ([""] = parse "" [collect [keep to <end>]])
    ([""] = parse "" [collect [keep across to <end>]])
]

[https://github.com/red/red/issues/3108
    (
        partition3108: function [elems [block!] size [integer!]] [
            return parse elems [
                collect some [not <end> ||
                    keep across repeat (size) <any>
                    | keep spread collect keep across to <end>
                ]        ; |------ this -----| spread collect keep is superflous
            ]
        ]
        [[1 2] [3 4] [5 6] [7 8] [9]] = partition3108 [1 2 3 4 5 6 7 8 9] 2
    )
    (
        partition3108: function [elems [block!] size [integer!]] [
            return parse elems [
                collect some [not <end> ||
                    keep across repeat (size) <any>
                    | keep across to <end>  ; equivalent, shorter
                ]
            ]
        ]
        [[1 2] [3 4] [5 6] [7 8] [9]] = partition3108 [1 2 3 4 5 6 7 8 9] 2
    )
]

[https://github.com/red/red/issues/4198
    ;
    ; !!! It's not clear whether Red's KEEP PICK is supposed to splice or not.
    ; Apparently not if a block comes from a group (?!)  UPARSE is much more
    ; consistent in its rules!
    ;
    ([[a b]] = parse [] [collect keep ([a b])])
    ([a] = parse [] [collect keep ('a)])
]

[
    (
        foo: func [value] [return value]
        res: parse [a 3 4 t [t 9] "test" 8] [
            collect [
                some [
                    keep integer!
                    | p: <here>, block!, seek (p), subparse any-series! [
                        keep collect [try some [
                            keep integer! keep ('+)
                            | <any> keep (foo '-)
                        ]]
                    ]
                    | <any>
                ]
            ]
        ]
        res = [3 4 [- 9 +] 8]
    )
]


https://github.com/metaeducation/ren-c/issues/939
(
    thing: ~
    did all [
        raised? parse "a" [thing: collect [foo: <here>, "a", keep seek (foo)]]
        foo = "a"
        thing = ["a"]
    ]
)


(
    data: [foo: "a" bar: "b" | foo: "c" bar: "d"]

    [[foo: "a" bar: "b"] [foo: "c" bar: "d"]] = parse data [
        collect some further [keep collect [
            [keep ['foo:], keep text!]
            [keep ['bar:], keep text!]
            ['| | <end>]
        ]]
    ]
)
