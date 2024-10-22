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
    result: ~
    all [
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
    x: ~
    rule: [x: "a"]
    all [
        "a" == parse "a" rule
        same? x second rule
    ]
)(
    x: ~
    rule: [x: "a"]
    data: ["a"]
    all [
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
        bincat: to-binary -{C😺T}-
        bincat = #{43F09F98BA54}
    )

    ("C😺T" == parse bincat [-{C😺T}-])

    ("c😺t" == parse bincat [-{c😺t}-])

    ~parse-mismatch~ !! (parse:case bincat [-{c😺t}- <end>])
]

(
    test: to-binary -{The C😺T Test}-
    all [
        let x: ~
        #{43F09F98BA54} == parse test [to -{c😺t}- x: across to space to <end>]
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)

[https://github.com/red/red/issues/678
    ("cat" == parse "catcatcatcat" [repeat 4 "cat"])
    ("cat" == parse "catcatcat" [repeat 3 "cat"])
    ("cat" == parse "catcat" [repeat 2 "cat"])
    ~parse-mismatch~ !! (parse "cat" [repeat 4 "cat"])
    ~parse-mismatch~ !! (parse "cat" [repeat 3 "cat"])
    ~parse-mismatch~ !! (parse "cat" [repeat 2 "cat"])
    ("cat" == parse "cat" [repeat 1 "cat"])
]

; String casing
[
    ("A" == parse "a" ["A"])
    ~parse-mismatch~ !! (parse "a" [#A])
    ~parse-mismatch~ !! (parse:case "a" ["A"])
    ~parse-mismatch~ !! (parse:case "a" [#A])
    ("a" == parse:case "a" ["a"])
    (#a == parse:case "a" [#a])
    ("A" == parse:case "A" ["A"])
    (#A == parse:case "A" [#A])
    ("test" == parse "TeSt" ["test"])
    ~parse-mismatch~ !! (parse:case "TeSt" ["test"])
    ("TeSt" == parse:case "TeSt" ["TeSt"])
]

; Casing in a block
[
    ("a" = parse:case ["a"] ["a"])
    ~parse-mismatch~ !! (parse:case ["a"] ["A"])
]

; String unicode
[
    (#é == parse "abcdé" [#a #b #c #d #é])
    ("abcdé" == parse "abcdé" ["abcdé"])
    ~parse-mismatch~ !! (parse "abcde" [#a #b #c #d #é])
    (#é == parse "abcdé" [#a #b #c #d #é])
    (#"✐" == parse "abcdé✐" [#a #b #c #d #é #"✐"])
    ("abcdé✐" == parse "abcdé✐" ["abcdé✐"])
    ~parse-mismatch~ !! (parse "abcdé" ["abcdé✐"])
    ~parse-mismatch~ !! (parse "ab✐cdé" ["abcdé✐"])
    ~parse-incomplete~ !! (parse "abcdé✐" ["abcdé"])
    ("✐abcdé" == parse "✐abcdé" ["✐abcdé"])
    (#"𐀀" == parse "abcdé✐𐀀" [#a #b #c #d #é #"✐" #"𐀀"])
    ("ab𐀀cdé✐" == parse "ab𐀀cdé✐" ["ab𐀀cdé✐"])
    ~parse-mismatch~ !! (parse "abcdé" ["abc𐀀dé"])
    ~parse-mismatch~ !! (parse "𐀀abcdé" ["a𐀀bcdé"])
    ~parse-incomplete~ !! (parse "abcdé𐀀" ["abcdé"])
    ("𐀀abcdé" == parse "𐀀abcdé" ["𐀀abcdé"])
]

[
    (
        str: "Lorem ipsum dolor sit amet."
        ok
    )

    (#"." == parse str [thru "amet" one])
    (
        res: ~
        all [
            "dolor" == parse str [
                thru "ipsum" one res: across to #" " to <end>
            ]
            res = "dolor"
        ]
    )
    (
        res: ~
        all [
            "sum dolor sit amet." == parse str [thru #p res: <here> to <end>]
            9 = index? res
        ]
    )
]
