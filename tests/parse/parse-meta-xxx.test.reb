; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but with "literalizing" result semantics.  If it
; was invisible, it gives ~void~.  This helps write higher level tools
; that might want to know about invisibility status.
[
    (did all  [
        then? uparse "" [synthesized: ^[]]  ; ~null~ isotope result (success)
        '~void~ = synthesized
    ])
    (did all  [
        then? uparse "" [synthesized: ^[('~void~)]]  ; not-NULL result
        (the '~void~) = synthesized  ; friendly if user made it friendly
    ])
    (did all  [
        then? uparse "" [synthesized: ^[(~void~)]]  ; not-NULL result
        '~void~ = synthesized  ; user didn't quote it, so suggests unfriendly
    ])
    ((the '~friendly~) = ^(uparse [~friendly~] [bad-word!]))
]
