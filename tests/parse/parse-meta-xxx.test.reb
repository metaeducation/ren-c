; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.

[
    (all  [
        nihil' = parse "" [synthesized: ^[]]
        nihil' = synthesized
    ])
    (all  [
        nihil' = parse "" [synthesized: ^[comment "hi"]]
        nihil' = synthesized
    ])
    (all  [
        '~()~ = parse "" [synthesized: ^[(~()~)]]
        '~()~ = synthesized
    ])
    ('~friendly~ = parse [~friendly~] [&quasi-word?])
]
