; %parse-meta-xxx.test.reb

; META-BLOCK! runs a rule, but with "literalizing" result semantics.  If it
; was invisible, it gives ~void~.  This helps write higher level tools
; that might want to know about invisibility status.
[
    (did all  [
        uparse? "" [synthesized: ^[]]  ; ~none~ isotope result (success)
        '~none~ = synthesized
    ])
    (did all  [
        uparse? "" [synthesized: ^[('~none~)]]  ; not-NULL result
        (the '~none~) = synthesized  ; friendly if user made it friendly
    ])
    (did all  [
        uparse? "" [synthesized: ^[(~none~)]]  ; not-NULL result
        '~none~ = synthesized  ; user didn't quote it, so suggests unfriendly
    ])
    ((the '~friendly~) = ^(uparse [~friendly~] [bad-word!]))
]
