; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but META-ifies the result.  Note that block rules
; cannot produce absolutely nothing, so it's not posisble to get ~void~ from
; this construct.  (Compare with META-GROUP!, which can produce ~void~.)

[
    (did all  [
        '~none~ == uparse "" [synthesized: ^[]]
        '~none~ = synthesized
    ])
    (did all  [
        '~none~ == uparse "" [synthesized: ^[comment "hi"]]  ; not ~void~
        '~none~ = synthesized
    ])
    (did all  [
        '~none~ == uparse "" [synthesized: ^[(~void~)]]
        '~none~ = synthesized
    ])
    ('~friendly~ = uparse [~friendly~] [bad-word!])
]
