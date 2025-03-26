; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.

[
    (all  [
        let synthesized
        '~[]~ = parse "" [synthesized: ^[]]
        '~[]~ = synthesized
    ])
    (all  [
        let synthesized
        '~[]~ = parse "" [synthesized: ^[comment "hi"]]
        '~[]~ = synthesized
    ])
    (all  [
        let synthesized
        '~()~ = parse "" [synthesized: ^[(~()~)]]
        '~()~ = synthesized
    ])
    ('~friendly~ = parse [~friendly~] [quasi-word?/])
]
