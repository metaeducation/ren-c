; %parse-text.test.reb
;
; A TEXT! rule will capture the actual match in a block.  But for a string, it
; will capture the *rule*.

; No-op rules need thought in UPARSE, in terms of NULL/BLANK! behavior.  But
; empty string should be a no-op on string input, and an empty rule should
; always match.
;

; rule capture: efficient (doesn't require a copy if you're not using it)
(
    rule: "cd"
    did all [
        "cd" = result: parse "abcd" ["ab" rule]
        "cdef" = append result "ef"
        "cdef" = rule
    ]
)

("" = parse "" [""])

("hello" == parse ["hello"] ["hello"])

("a" == parse "a" ["a"])
("ab" == parse "ab" ["ab"])
("abc" == parse "abc" ["abc"])
("abc" == parse "abc" ["abc" <end>])

; Ren-C does not mandate that rules make progress, so matching empty strings
; works, as it does in Red.
[
    ("ab" == parse "ab" [to [""] "ab"])
    ("ab" == parse "ab" [to ["a"] "ab"])
    ("ab" == parse "ab" [to ["ab"] "ab"])
    ("ab" == parse "ab" [thru [""] "ab"])
    ("b" == parse "ab" [thru ["a"] "b"])
    ("" == parse "ab" [thru ["ab"] ""])
]

[(
    rule: [x: "a"]
    did all [
        "a" == parse "a" rule
        same? x second rule
    ]
)(
    data: ["a"]
    rule: [x: "a"]
    did all [
        "a" == parse data rule
        same? x first data
    ]
)]

; Multi-byte characters and strings present a lot of challenges.  There should
; be many more tests and philosophies written up of what the semantics are,
; especially when it comes to BINARY! and ANY-STRING? mixtures.  These tests
; are better than nothing...
[
    (
        catchar: #"🐱"
        #🐱 == parse #{F09F90B1} [catchar]
    )
    (
        cattext: "🐱"
        "🐱" == parse #{F09F90B1} [cattext]
    )
    ~find-string-binary~ !! (
        catbin: #{F09F90B1}
        parse "🐱" [catbin]
    )
    (
        catchar: #"🐱"
        #🐱 == parse "🐱" [catchar]
    )
]

[
    (
        bincat: to-binary {C😺T}
        bincat = #{43F09F98BA54}
    )

    ("C😺T" == parse bincat [{C😺T}])

    ("c😺t" == parse bincat [{c😺t}])

    (raised? parse/case bincat [{c😺t} <end>])
]

(
    test: to-binary {The C😺T Test}
    did all [
        #{43F09F98BA54} == parse test [to {c😺t} x: across to space to <end>]
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)

[https://github.com/red/red/issues/678
    ("cat" == parse "catcatcatcat" [repeat 4 "cat"])
    ("cat" == parse "catcatcat" [repeat 3 "cat"])
    ("cat" == parse "catcat" [repeat 2 "cat"])
    (raised? parse "cat" [repeat 4 "cat"])
    (raised? parse "cat" [repeat 3 "cat"])
    (raised? parse "cat" [repeat 2 "cat"])
    ("cat" == parse "cat" [repeat 1 "cat"])
]

; String casing
[
    ("A" == parse "a" ["A"])
    (raised? parse "a" [#A])
    (raised? parse/case "a" ["A"])
    (raised? parse/case "a" [#A])
    ("a" == parse/case "a" ["a"])
    (#a == parse/case "a" [#a])
    ("A" == parse/case "A" ["A"])
    (#A == parse/case "A" [#A])
    ("test" == parse "TeSt" ["test"])
    (raised? parse/case "TeSt" ["test"])
    ("TeSt" == parse/case "TeSt" ["TeSt"])
]

; String unicode
[
    (#é == parse "abcdé" [#a #b #c #d #é])
    ("abcdé" == parse "abcdé" ["abcdé"])
    (raised? parse "abcde" [#a #b #c #d #é])
    (#é == parse "abcdé" [#a #b #c #d #é])
    (#"✐" == parse "abcdé✐" [#a #b #c #d #é #"✐"])
    ("abcdé✐" == parse "abcdé✐" ["abcdé✐"])
    (raised? parse "abcdé" ["abcdé✐"])
    (raised? parse "ab✐cdé" ["abcdé✐"])
    (raised? parse "abcdé✐" ["abcdé"])
    ("✐abcdé" == parse "✐abcdé" ["✐abcdé"])
    (#"𐀀" == parse "abcdé✐𐀀" [#a #b #c #d #é #"✐" #"𐀀"])
    ("ab𐀀cdé✐" == parse "ab𐀀cdé✐" ["ab𐀀cdé✐"])
    (raised? parse "abcdé" ["abc𐀀dé"])
    (raised? parse "𐀀abcdé" ["a𐀀bcdé"])
    (raised? parse "abcdé𐀀" ["abcdé"])
    ("𐀀abcdé" == parse "𐀀abcdé" ["𐀀abcdé"])
]

[
    (
        str: "Lorem ipsum dolor sit amet."
        true
    )

    (#. == parse str [thru "amet" <any>])
    (
        res: ~
        did all [
            "dolor" == parse str [
                thru "ipsum" <any> res: across to #" " to <end>
            ]
            res = "dolor"
        ]
    )
    (
        res: ~
        did all [
            "sum dolor sit amet." == parse str [thru #p res: <here> to <end>]
            9 = index? res
        ]
    )
]
