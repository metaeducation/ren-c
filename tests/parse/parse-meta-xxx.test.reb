; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.

[
    (did all  [
        '~blank~ == ^ uparse "" [synthesized: ^[]]
        _ = synthesized
    ])
    (did all  [
        '~blank~ == ^ uparse "" [synthesized: ^[comment "hi"]]
        _ = synthesized
    ])
    (did all  [
        '~void~ == uparse "" [synthesized: ^[(~void~)]]
        '~void~ = synthesized
    ])
    ('~friendly~ = uparse [~friendly~] [bad-word!])
]
