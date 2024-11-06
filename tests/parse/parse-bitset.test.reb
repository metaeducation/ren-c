; %parse-bitset.test.reb
;
; A BITSET! can act like a charset or like a "byteset" for a BLOB!.

(
    byteset: make bitset! [0 16 32]
    32 == parse #{001020} [some byteset]
)

[#206 (
    any-char: complement charset ""
    count-up 'n 512 [
        if n = 1 [continue]

        let c
        if raised? parse (append copy "" make-char n - 1) [
            c: any-char <end>
        ][
            fail "Parse didn't work"
        ]
        if c != make-char n - 1 [fail "Char didn't match"]
    ]
    ok
)]

; With strings, BITSET! acts as charset
; (from %parse-test.red)
[
    (
        bs: charset [not 1 - 3 #"^/" - #"^O"]
        wbs: [bs]
        wbs2: reduce wbs
        ok
    )

    ~parse-mismatch~ !! (parse #{0A0B0C} [some bs])
    ~parse-mismatch~ !! (parse #{010203} [some bs])
    ~parse-mismatch~ !! (parse #{0A0B0C} [bs bs bs])
    ~parse-mismatch~ !! (parse #{010203} [bs bs bs])

    (
        digit: charset [0 - 9]
        all [
            let p
            #{010203} = parse #{0BADCAFE010203} [to digit p: <here> skip 3]
            p = #{010203}
        ]
    )
]

; With BLOB!, bitset acts as byteset
[
    (
        bs: charset [16 - 31 #"^/" - #"^O"]
        wbs: [bs]
        wbs2: reduce wbs
        ok
    )

    (12 == parse #{0A0B0C} [some bs])
    ~parse-mismatch~ !! (parse #{010203} [some bs])

    (12 == parse #{0A0B0C} [some [bs]])
    ~parse-mismatch~ !! (parse #{010203} [some [bs]])

    (12 == parse #{0A0B0C} [some wbs])
    ~parse-mismatch~ !! (parse #{010203} [some wbs])

    (12 == parse #{0A0B0C} [some wbs2])
    ~parse-mismatch~ !! (parse #{010203} [some wbs2])

    (12 == parse #{0A0B0C} [bs bs bs])
    ~parse-mismatch~ !! (parse #{010203} [bs bs bs])

    (12 == parse #{0A0B0C} [[bs] [bs] [bs]])
    ~parse-mismatch~ !! (parse #{010203} [[bs] [bs] [bs]])

    (12 == parse #{0A0B0C} [wbs wbs wbs])
    ~parse-mismatch~ !! (parse #{010203} [wbs wbs wbs])

    (12 == parse #{0A0B0C} [wbs2 wbs2 wbs2])
    ~parse-mismatch~ !! (parse #{010203} [wbs2 wbs2 wbs2])
]

[#753
    (
        b: ~
        ws: to-bitset unspaced [tab newline CR SP]
        abc: charset ["a" "b" "c"]
        rls: ["a", some ws, b: across some abc, some ws, "c"]
        rla: ["a", opt some ws, b: across some abc, opt some ws, "c"]
        ok
    )

    ("c" == parse "a b c" rls)
    ("c" == parse "a b c" rla)

    ~parse-mismatch~ !! (parse "a b" rls)
    ~parse-mismatch~ !! (parse "a b" rla)
]

[#1298 (
    cset: charset [#"^(01)" - #"^(FF)"]
    "a" = parse "a" ["a" elide opt some cset]
)(
    cset: charset [# - #"^(FE)"]
    null = parse "a" ["a" opt some cset]
)(
    cset: charset [# - #"^(FF)"]
    "a" = parse "a" ["a" elide opt some cset]
)]

[
    (
        bs: charset ["hello" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        ok
    )

    (#c == parse "abc" [some bs])
    ~parse-mismatch~ !! (parse "123" [some bs])
    ~parse-mismatch~ !! (parse "ABC" [some bs])

    (#c == parse "abc" [some [bs]])
    ~parse-mismatch~ !! (parse "123" [some [bs]])

    (#c == parse "abc" [some wbs])
    ~parse-mismatch~ !! (parse "123" [some wbs])

    (#c == parse "abc" [some wbs2])
    ~parse-mismatch~ !! (parse "123" [some wbs2])

    (#c == parse "abc" [bs bs bs])
    ~parse-mismatch~ !! (parse "123" [bs bs bs])

    (#c == parse "abc" [[bs] [bs] [bs]])
    ~parse-mismatch~ !! (parse "123" [[bs] [bs] [bs]])

    (#c == parse "abc" [wbs wbs wbs])
    ~parse-mismatch~ !! (parse "123" [wbs wbs wbs])

    (#c == parse "abc" [wbs2 wbs2 wbs2])
    ~parse-mismatch~ !! (parse "123" [wbs2 wbs2 wbs2])
]

[
    (
        bs: charset [not "hello123" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        ok
    )

    ~parse-mismatch~ !! (parse "abc" [some bs])
    (#C == parse "ABC" [some bs])

    ~parse-mismatch~ !! (parse "123" [some bs])
    (#9 == parse "789" [some bs])

    ~parse-mismatch~ !! (parse "abc" [bs bs bs])
    (#C == parse "ABC" [bs bs bs])

    ~parse-mismatch~ !! (parse "123" [bs bs bs])
    (#9 == parse "789" [bs bs bs])

    (
        digit: charset "0123456789"
        all [
            let p
            "123" = parse "hello 123" [to digit p: <here> skip 3]
            p = "123"
        ]
    )
]
