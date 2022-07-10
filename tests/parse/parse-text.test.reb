; %parse-text.test.reb
;
; A TEXT! rule will capture the actual match in a block.  But for a string, it
; will capture the *rule*.

; No-op rules need thought in UPARSE, in terms of NULL/BLANK! behavior.  But
; empty string should be a no-op on string input, and an empty rule should
; always match.
;

("" = parse "" [""])

("hello" == parse ["hello"] ["hello"])

("a" == parse "a" ["a"])
("ab" == parse "ab" ["ab"])
("abc" == parse "abc" ["abc"])
("" == parse "abc" ["abc" <end>])

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
; especially when it comes to BINARY! and ANY-STRING! mixtures.  These tests
; are better than nothing...
(
    catchar: #"🐱"
    #🐱 == parse #{F09F90B1} [catchar]
)(
    cattext: "🐱"
    "🐱" == parse #{F09F90B1} [cattext]
)(
    catbin: #{F09F90B1}
    e: trap [parse "🐱" [catbin]]
    'find-string-binary = e.id
)(
    catchar: #"🐱"
    #🐱 == parse "🐱" [catchar]
)

[
    (
        bincat: to-binary {C😺T}
        bincat = #{43F09F98BA54}
    )

    ("C😺T" == parse bincat [{C😺T}])

    ("c😺t" == parse bincat [{c😺t}])

    (didn't parse/case bincat [{c😺t} <end>])
]

(
    test: to-binary {The C😺T Test}
    did all [
        #{} == parse test [to {c😺t} x: across to space to <end>]
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)

[https://github.com/red/red/issues/678
    ("cat" == parse "catcatcatcat" [4 "cat"])
    ("cat" == parse "catcatcat" [3 "cat"])
    ("cat" == parse "catcat" [2 "cat"])
    (didn't parse "cat" [4 "cat"])
    (didn't parse "cat" [3 "cat"])
    (didn't parse "cat" [2 "cat"])
    ("cat" == parse "cat" [1 "cat"])
]

; String casing
[
    ("A" == parse "a" ["A"])
    (didn't parse "a" [#A])
    (didn't parse/case "a" ["A"])
    (didn't parse/case "a" [#A])
    ("a" == parse/case "a" ["a"])
    (#a == parse/case "a" [#a])
    ("A" == parse/case "A" ["A"])
    (#A == parse/case "A" [#A])
    ("test" == parse "TeSt" ["test"])
    (didn't parse/case "TeSt" ["test"])
    ("TeSt" == parse/case "TeSt" ["TeSt"])
]

; String unicode
[
    (#é == parse "abcdé" [#a #b #c #d #é])
    ("abcdé" == parse "abcdé" ["abcdé"])
    (didn't parse "abcde" [#a #b #c #d #é])
    (#é == parse "abcdé" [#a #b #c #d #é])
    (#"✐" == parse "abcdé✐" [#a #b #c #d #é #"✐"])
    ("abcdé✐" == parse "abcdé✐" ["abcdé✐"])
    (didn't parse "abcdé" ["abcdé✐"])
    (didn't parse "ab✐cdé" ["abcdé✐"])
    (didn't parse "abcdé✐" ["abcdé"])
    ("✐abcdé" == parse "✐abcdé" ["✐abcdé"])
    (#"𐀀" == parse "abcdé✐𐀀" [#a #b #c #d #é #"✐" #"𐀀"])
    ("ab𐀀cdé✐" == parse "ab𐀀cdé✐" ["ab𐀀cdé✐"])
    (didn't parse "abcdé" ["abc𐀀dé"])
    (didn't parse "𐀀abcdé" ["a𐀀bcdé"])
    (didn't parse "abcdé𐀀" ["abcdé"])
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
            "" == parse str [thru "ipsum" <any> res: across to #" " to <end>]
            res = "dolor"
        ]
    )
    (
        res: ~
        did all [
            "" == parse str [thru #p res: <here> to <end>]
            9 = index? res
        ]
    )
]
