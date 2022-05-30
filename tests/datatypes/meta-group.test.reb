; The META-GROUP! type is new and needs testing.

(meta-group! = type of '^(a b c))

; The only way to see vanishing stuff is with meta operations, which give you
; BLANK! at the meta level in the case of full erasure.
[
    (blank? ^())
    (blank? ^(comment "hi"))
    (blank? ^(void))

    (blank? ^(maybe comment "hi"))
    (blank? ^(maybe void))

    (blank? ^ ())
    (blank? ^ (comment "hi"))
    (blank? ^ (void))

    (blank? ^ (maybe comment "hi"))
    (blank? ^ (maybe void))
]

((the '10) = ^(10 comment "hi"))

(null = ^(null))
('~null~ = ^(if true [null]))

((the '1020) = ^(1000 + 20))
