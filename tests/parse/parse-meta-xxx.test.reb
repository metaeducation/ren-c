; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.

[
    (all  [
        '~[]~ = parse "" [synthesized: ^[]]
        '~[]~ = synthesized
    ])
    (all  [
        '~[]~ = parse "" [synthesized: ^[comment "hi"]]
        '~[]~ = synthesized
    ])
    (all  [
        '~()~ = parse "" [synthesized: ^[(~()~)]]
        '~()~ = synthesized
    ])
    ('~friendly~ = parse [~friendly~] [&quasi-word?])
]
