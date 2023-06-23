; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.

[
    (did all  [
        void' = parse "" [synthesized: ^[]]
        void' = synthesized
    ])
    (did all  [
        void' = parse "" [synthesized: ^[comment "hi"]]
        void' = synthesized
    ])
    (did all  [
        '~()~ = parse "" [synthesized: ^[(~()~)]]
        '~()~ = synthesized
    ])
    ('~friendly~ = parse [~friendly~] [quasi-word!])
]
