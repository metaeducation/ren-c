; The META-GROUP! type is new and needs testing.

(meta-group! = type of '^(a b c))

; The only way to see vanishing stuff is with meta operations.
[
    ('~void~ = ^())
    ('~void~ = ^(comment "hi"))
    ('~void~ = ^(void))

    ('~void~ = ^(maybe comment "hi"))
    ('~void~ = ^(maybe void))

    ('~void~ = ^ ())
    ('~void~ = ^ (comment "hi"))
    ('~void~ = ^ (void))

    ('~void~ = ^ (maybe comment "hi"))
    ('~void~ = ^ (maybe void))
]

((the '10) = ^(10 comment "hi"))

(null = ^(null))
('~null~ = ^(if true [null]))

((the '1020) = ^(1000 + 20))
