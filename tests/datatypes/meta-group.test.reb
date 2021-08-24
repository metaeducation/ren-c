; The META-GROUP! type is new and needs testing.

(meta-group! = type of '^(a b c))

; ENDs make unfriendly voids when meta quoted, that if you further meta
; quote it will make friendly ones.
[
    ('~void~ = ^ ^())
    ('~void~ = ^ ^(comment "hi"))
    ('~void~ = ^ ^(void))

    ('~void~ = reify ^())
    ('~void~ = reify ^(comment "hi"))
    ('~void~ = reify ^(void))
]

((the '10) = ^(10 comment "hi"))

(null = ^(null))
('~null~ = ^(if true [null]))

((the '1020) = ^(1000 + 20))
