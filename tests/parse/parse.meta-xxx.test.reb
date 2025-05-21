; %parse-meta-xxx.test.reb

; ^BLOCK! runs a rule, but unlifts the result.

[
    (all  [
        let synthesized
        (the '3) = parse "" [synthesized: ^[(lift 1 + 2)]]
        (the '3) = synthesized
    ])
]
