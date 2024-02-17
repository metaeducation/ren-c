; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.

[
    (did all  [
        nihil' = parse "" [synthesized: ^[]]
        nihil' = synthesized
    ])
    (did all  [
        nihil' = parse "" [synthesized: ^[comment "hi"]]
        nihil' = synthesized
    ])
    (did all  [
        '~()~ = parse "" [synthesized: ^[(~()~)]]
        '~()~ = synthesized
    ])
    ('~friendly~ = parse [~friendly~] [&quasi-word?])
]
