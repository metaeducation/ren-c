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

        if not uparse? (append copy "" make char! n - 1) [c: any-char end] [
            fail "Parse didn't work"
        ]
        if c != make char! n - 1 [fail "Char didn't match"]
    ]
    true
)]
