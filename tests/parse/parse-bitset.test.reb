; %parse-bitset.test.reb
;
; A BITSET! can act like a charset or like a "byteset" for a BINARY!.

(
    byteset: make bitset! [0 16 32]
    32 == uparse #{001020} [some byteset]
)

[#206 (
    any-char: complement charset ""
    count-up n 512 [
        if n = 1 [continue]

        if didn't uparse (append copy "" make char! n - 1) [c: any-char <end>] [
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
    (didn't uparse #{0A0B0C} [some bs])
    (didn't uparse #{010203} [some bs])
    (9 == uparse #{070809} [some bs])
    (didn't uparse #{0A0B0C} [bs bs bs])
    (didn't uparse #{010203} [bs bs bs])
    (9 == uparse #{070809} [bs bs bs])
    (
        digit: charset [0 - 9]
        did all [
            3 == uparse #{0BADCAFE010203} [to digit p: <here> 3 <any>]
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
    (12 == uparse #{0A0B0C} [some bs])
    (didn't uparse #{010203} [some bs])
    (12 == uparse #{0A0B0C} [some [bs]])
    (didn't uparse #{010203} [some [bs]])
    (12 == uparse #{0A0B0C} [some wbs])
    (didn't uparse #{010203} [some wbs])
    (12 == uparse #{0A0B0C} [some wbs2])
    (didn't uparse #{010203} [some wbs2])
    (12 == uparse #{0A0B0C} [bs bs bs])
    (didn't uparse #{010203} [bs bs bs])
    (12 == uparse #{0A0B0C} [[bs] [bs] [bs]])
    (didn't uparse #{010203} [[bs] [bs] [bs]])
    (12 == uparse #{0A0B0C} [wbs wbs wbs])
    (didn't uparse #{010203} [wbs wbs wbs])
    (12 == uparse #{0A0B0C} [wbs2 wbs2 wbs2])
    (didn't uparse #{010203} [wbs2 wbs2 wbs2])
]

[#753
    (
        ws: to-bitset unspaced [tab newline cr sp]
        abc: charset ["a" "b" "c"]
        rls: ["a", some ws, b: across some abc, some ws, "c"]
        rla: ["a", opt some ws, b: across some abc, opt some ws, "c"]
        true
    )
    ("c" == uparse "a b c" rls)
    ("c" == uparse "a b c" rla)
    (didn't uparse "a b" rls)
    (didn't uparse "a b" rla)
]

[#1298 (
    cset: charset [#"^(01)" - #"^(FF)"]
    "a" = uparse "a" ["a" maybe some cset]
)(
    cset: charset [# - #"^(FE)"]
    '~null~ = ^ uparse "a" ["a" opt some cset]
)(
    cset: charset [# - #"^(FF)"]
    "a" = uparse "a" ["a" maybe some cset]
)]

[
    (
        bs: charset ["hello" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (#c == uparse "abc" [some bs])
    (didn't uparse "123" [some bs])
    (didn't uparse "ABC" [some bs])
    (#c == uparse "abc" [some [bs]])
    (didn't uparse "123" [some [bs]])
    (#c == uparse "abc" [some wbs])
    (didn't uparse "123" [some wbs])
    (#c == uparse "abc" [some wbs2])
    (didn't uparse "123" [some wbs2])
    (#c == uparse "abc" [bs bs bs])
    (didn't uparse "123" [bs bs bs])
    (#c == uparse "abc" [[bs] [bs] [bs]])
    (didn't uparse "123" [[bs] [bs] [bs]])
    (#c == uparse "abc" [wbs wbs wbs])
    (didn't uparse "123" [wbs wbs wbs])
    (#c == uparse "abc" [wbs2 wbs2 wbs2])
    (didn't uparse "123" [wbs2 wbs2 wbs2])
]

[
    (
        bs: charset [not "hello123" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (didn't uparse "abc" [some bs])
    (#C == uparse "ABC" [some bs])
    (didn't uparse "123" [some bs])
    (#9 == uparse "789" [some bs])
    (didn't uparse "abc" [bs bs bs])
    (#C == uparse "ABC" [bs bs bs])
    (didn't uparse "123" [bs bs bs])
    (#9 == uparse "789" [bs bs bs])
    (
        digit: charset "0123456789"
        did all [
            #3 == uparse "hello 123" [to digit p: <here> 3 <any>]
            p = "123"
        ]
    )
]
