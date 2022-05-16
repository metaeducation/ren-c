; %parse-text.test.reb
;
; A TEXT! rule will capture the actual match in a block.  But for a string, it
; will capture the *rule*.

; No-op rules need thought in UPARSE, in terms of NULL/BLANK! behavior.  But
; empty string should be a no-op on string input, and an empty rule should
; always match.
;

("" = uparse "" [""])

("hello" == uparse ["hello"] ["hello"])

("a" == uparse "a" ["a"])
("ab" == uparse "ab" ["ab"])
("abc" == uparse "abc" ["abc"])
("" == uparse "abc" ["abc" <end>])

; Ren-C does not mandate that rules make progress, so matching empty strings
; works, as it does in Red.
[
    ("ab" == uparse "ab" [to [""] "ab"])
    ("ab" == uparse "ab" [to ["a"] "ab"])
    ("ab" == uparse "ab" [to ["ab"] "ab"])
    ("ab" == uparse "ab" [thru [""] "ab"])
    ("b" == uparse "ab" [thru ["a"] "b"])
    ("" == uparse "ab" [thru ["ab"] ""])
]

[(
    rule: [x: "a"]
    did all [
        "a" == uparse "a" rule
        same? x second rule
    ]
)(
    data: ["a"]
    rule: [x: "a"]
    did all [
        "a" == uparse data rule
        same? x first data
    ]
)]

; Multi-byte characters and strings present a lot of challenges.  There should
; be many more tests and philosophies written up of what the semantics are,
; especially when it comes to BINARY! and ANY-STRING! mixtures.  These tests
; are better than nothing...
(
    catchar: #"🐱"
    #🐱 == uparse #{F09F90B1} [catchar]
)(
    cattext: "🐱"
    "🐱" == uparse #{F09F90B1} [cattext]
)(
    catbin: #{F09F90B1}
    e: trap [uparse "🐱" [catbin]]
    'find-string-binary = e.id
)(
    catchar: #"🐱"
    #🐱 == uparse "🐱" [catchar]
)

[
    (
        bincat: to-binary {C😺T}
        bincat = #{43F09F98BA54}
    )

    ("C😺T" == uparse bincat [{C😺T}])

    ("c😺t" == uparse bincat [{c😺t}])

    (didn't uparse/case bincat [{c😺t} <end>])
]

(
    test: to-binary {The C😺T Test}
    did all [
        #{} == uparse test [to {c😺t} x: across to space to <end>]
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)

[https://github.com/red/red/issues/678
    ("cat" == uparse "catcatcatcat" [4 "cat"])
    ("cat" == uparse "catcatcat" [3 "cat"])
    ("cat" == uparse "catcat" [2 "cat"])
    (didn't uparse "cat" [4 "cat"])
    (didn't uparse "cat" [3 "cat"])
    (didn't uparse "cat" [2 "cat"])
    ("cat" == uparse "cat" [1 "cat"])
]

; String casing
[
    ("A" == uparse "a" ["A"])
    (didn't uparse "a" [#A])
    (didn't uparse/case "a" ["A"])
    (didn't uparse/case "a" [#A])
    ("a" == uparse/case "a" ["a"])
    (#a == uparse/case "a" [#a])
    ("A" == uparse/case "A" ["A"])
    (#A == uparse/case "A" [#A])
    ("test" == uparse "TeSt" ["test"])
    (didn't uparse/case "TeSt" ["test"])
    ("TeSt" == uparse/case "TeSt" ["TeSt"])
]

; String unicode
[
    (#é == uparse "abcdé" [#a #b #c #d #é])
    ("abcdé" == uparse "abcdé" ["abcdé"])
    (didn't uparse "abcde" [#a #b #c #d #é])
    (#é == uparse "abcdé" [#a #b #c #d #é])
    (#"✐" == uparse "abcdé✐" [#a #b #c #d #é #"✐"])
    ("abcdé✐" == uparse "abcdé✐" ["abcdé✐"])
    (didn't uparse "abcdé" ["abcdé✐"])
    (didn't uparse "ab✐cdé" ["abcdé✐"])
    (didn't uparse "abcdé✐" ["abcdé"])
    ("✐abcdé" == uparse "✐abcdé" ["✐abcdé"])
    (#"𐀀" == uparse "abcdé✐𐀀" [#a #b #c #d #é #"✐" #"𐀀"])
    ("ab𐀀cdé✐" == uparse "ab𐀀cdé✐" ["ab𐀀cdé✐"])
    (didn't uparse "abcdé" ["abc𐀀dé"])
    (didn't uparse "𐀀abcdé" ["a𐀀bcdé"])
    (didn't uparse "abcdé𐀀" ["abcdé"])
    ("𐀀abcdé" == uparse "𐀀abcdé" ["𐀀abcdé"])
]

[
    (
        str: "Lorem ipsum dolor sit amet."
        true
    )

    (#. == uparse str [thru "amet" <any>])
    (
        res: ~
        did all [
            "" == uparse str [thru "ipsum" <any> res: across to #" " to <end>]
            res = "dolor"
        ]
    )
    (
        res: ~
        did all [
            "" == uparse str [thru #p res: <here> to <end>]
            9 = index? res
        ]
    )
]
