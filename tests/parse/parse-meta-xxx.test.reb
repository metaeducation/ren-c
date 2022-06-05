; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.

[
    (did all  [
        @void = uparse "" [synthesized: ^[]]
        @void = synthesized
    ])
    (did all  [
        @void = uparse "" [synthesized: ^[comment "hi"]]
        @void = synthesized
    ])
    (did all  [
        '~void~ == uparse "" [synthesized: ^[(~void~)]]
        '~void~ = synthesized
    ])
    ('~friendly~ = uparse [~friendly~] [bad-word!])
]
