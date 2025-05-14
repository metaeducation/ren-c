; %parse-meta-xxx.test.reb

; ^BLOCK! runs a rule, but unmetas the result.

[
    (all  [
        let synthesized
        (the '3) = parse "" [synthesized: ^[(meta 1 + 2)]]
        (the '3) = synthesized
    ])
]
