; %parse-bitset.test.reb
;
; A BITSET! can act like a charset or like a "byteset" for a BINARY!.

(
    byteset: make bitset! [0 16 32]
    32 == parse #{001020} [some byteset]
)

[#206 (
    any-char: complement charset ""
    count-up n 512 [
        if n = 1 [continue]

        if didn't parse (append copy "" codepoint-to-char n - 1) [
            c: any-char <end>
        ][
            fail "Parse didn't work"
        ]
        if c != codepoint-to-char n - 1 [fail "Char didn't match"]
    ]
    true
)]

; With strings, BITSET! acts as charset
; (from %parse-test.red)
[
    (
        bs: charset [not 1 - 3 #"^/" - #"^O"]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (didn't parse #{0A0B0C} [some bs])
    (didn't parse #{010203} [some bs])
    (9 == parse #{070809} [some bs])
    (didn't parse #{0A0B0C} [bs bs bs])
    (didn't parse #{010203} [bs bs bs])
    (9 == parse #{070809} [bs bs bs])
    (
        digit: charset [0 - 9]
        did all [
            #{010203} = parse #{0BADCAFE010203} [to digit p: <here> skip 3]
            p = #{010203}
        ]
    )
]

; With BINARY!, bitset acts as byteset
[
    (
        bs: charset [16 - 31 #"^/" - #"^O"]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (12 == parse #{0A0B0C} [some bs])
    (didn't parse #{010203} [some bs])
    (12 == parse #{0A0B0C} [some [bs]])
    (didn't parse #{010203} [some [bs]])
    (12 == parse #{0A0B0C} [some wbs])
    (didn't parse #{010203} [some wbs])
    (12 == parse #{0A0B0C} [some wbs2])
    (didn't parse #{010203} [some wbs2])
    (12 == parse #{0A0B0C} [bs bs bs])
    (didn't parse #{010203} [bs bs bs])
    (12 == parse #{0A0B0C} [[bs] [bs] [bs]])
    (didn't parse #{010203} [[bs] [bs] [bs]])
    (12 == parse #{0A0B0C} [wbs wbs wbs])
    (didn't parse #{010203} [wbs wbs wbs])
    (12 == parse #{0A0B0C} [wbs2 wbs2 wbs2])
    (didn't parse #{010203} [wbs2 wbs2 wbs2])
]

[#753
    (
        ws: to-bitset unspaced [tab newline cr sp]
        abc: charset ["a" "b" "c"]
        rls: ["a", some ws, b: across some abc, some ws, "c"]
        rla: ["a", try some ws, b: across some abc, try some ws, "c"]
        true
    )
    ("c" == parse "a b c" rls)
    ("c" == parse "a b c" rla)
    (didn't parse "a b" rls)
    (didn't parse "a b" rla)
]

[#1298 (
    cset: charset [#"^(01)" - #"^(FF)"]
    "a" = parse "a" ["a" try some cset]
)(
    cset: charset [# - #"^(FE)"]
    '~[~null~]~ = ^ parse "a" ["a" try some cset]
)(
    cset: charset [# - #"^(FF)"]
    "a" = parse "a" ["a" try some cset]
)]

[
    (
        bs: charset ["hello" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (#c == parse "abc" [some bs])
    (didn't parse "123" [some bs])
    (didn't parse "ABC" [some bs])
    (#c == parse "abc" [some [bs]])
    (didn't parse "123" [some [bs]])
    (#c == parse "abc" [some wbs])
    (didn't parse "123" [some wbs])
    (#c == parse "abc" [some wbs2])
    (didn't parse "123" [some wbs2])
    (#c == parse "abc" [bs bs bs])
    (didn't parse "123" [bs bs bs])
    (#c == parse "abc" [[bs] [bs] [bs]])
    (didn't parse "123" [[bs] [bs] [bs]])
    (#c == parse "abc" [wbs wbs wbs])
    (didn't parse "123" [wbs wbs wbs])
    (#c == parse "abc" [wbs2 wbs2 wbs2])
    (didn't parse "123" [wbs2 wbs2 wbs2])
]

[
    (
        bs: charset [not "hello123" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (didn't parse "abc" [some bs])
    (#C == parse "ABC" [some bs])
    (didn't parse "123" [some bs])
    (#9 == parse "789" [some bs])
    (didn't parse "abc" [bs bs bs])
    (#C == parse "ABC" [bs bs bs])
    (didn't parse "123" [bs bs bs])
    (#9 == parse "789" [bs bs bs])
    (
        digit: charset "0123456789"
        did all [
            "123" = parse "hello 123" [to digit p: <here> skip 3]
            p = "123"
        ]
    )
]
