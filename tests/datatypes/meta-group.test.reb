; The META-GROUP! type is new and needs testing.

(meta-group! = type of '^(a b c))

; The only way to see vanishing stuff is with meta operations, which give you
; ~void~ BAD-WORD!s...there is no way to otherwise observe the existence of
; a "void isotope".
[
    ('~void~ = ^())
    ('~void~ = ^(comment "hi"))
    ('~void~ = ^(void))
]

((the '10) = ^(10 comment "hi"))

(null = ^(null))
('~null~ = ^(if true [null]))

((the '1020) = ^(1000 + 20))
