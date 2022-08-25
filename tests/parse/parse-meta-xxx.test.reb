; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.

[
    (did all  [
        '~ = parse "" [synthesized: ^[]]
        void' = synthesized
    ])
    (did all  [
        '~ = parse "" [synthesized: ^[comment "hi"]]
        void' = synthesized
    ])
    (did all  [
        '~void~ = parse "" [synthesized: ^[(~void~)]]
        '~void~ = synthesized
    ])
    ('~friendly~ = parse [~friendly~] [bad-word!])
]
