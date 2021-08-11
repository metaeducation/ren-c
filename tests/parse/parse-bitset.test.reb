; %parse-bitset.test.reb
;
; A BITSET! can act like a charset or like a "byteset" for a BINARY!.

(
    byteset: make bitset! [0 16 32]
    uparse? #{001020} [some byteset]
)

[#206 (
    any-char: complement charset ""
    count-up n 512 [
        if n = 1 [continue]

        if not uparse? (append copy "" make char! n - 1) [c: any-char <end>] [
            fail "Parse didn't work"
        ]
        if c != make char! n - 1 [fail "Char didn't match"]
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
    (not uparse? #{0A0B0C} [some bs])
    (not uparse? #{010203} [some bs])
    (uparse? #{070809} [some bs])
    (not uparse? #{0A0B0C} [bs bs bs])
    (not uparse? #{010203} [bs bs bs])
    (uparse? #{070809} [bs bs bs])
    (
        digit: charset [0 - 9]
        did all [
            uparse? #{0BADCAFE010203} [to digit p: <here> 3 <any>]
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
    (uparse? #{0A0B0C} [some bs])
    (not uparse? #{010203} [some bs])
    (uparse? #{0A0B0C} [some [bs]])
    (not uparse? #{010203} [some [bs]])
    (uparse? #{0A0B0C} [some wbs])
    (not uparse? #{010203} [some wbs])
    (uparse? #{0A0B0C} [some wbs2])
    (not uparse? #{010203} [some wbs2])
    (uparse? #{0A0B0C} [bs bs bs])
    (not uparse? #{010203} [bs bs bs])
    (uparse? #{0A0B0C} [[bs] [bs] [bs]])
    (not uparse? #{010203} [[bs] [bs] [bs]])
    (uparse? #{0A0B0C} [wbs wbs wbs])
    (not uparse? #{010203} [wbs wbs wbs])
    (uparse? #{0A0B0C} [wbs2 wbs2 wbs2])
    (not uparse? #{010203} [wbs2 wbs2 wbs2])
]

[#753
    (
        ws: to-bitset unspaced [tab newline cr sp]
        abc: charset ["a" "b" "c"]
        rls: ["a" some ws b: across some abc some ws "c"]
        rla: ["a" while ws b: across some abc while ws "c"]
        true
    )
    (uparse? "a b c" rls)
    (uparse? "a b c" rla)
    (not uparse? "a b" rls)
    (not uparse? "a b" rla)
]

[#1298 (
    cset: charset [#"^(01)" - #"^(FF)"]
    uparse? "a" ["a" while cset]
)(
    cset: charset [# - #"^(FE)"]
    uparse? "a" ["a" while cset]
)(
    cset: charset [# - #"^(FF)"]
    uparse? "a" ["a" while cset]
)]
