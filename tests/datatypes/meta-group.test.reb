; The META-GROUP! type is new and needs testing.

(meta-group! = type of '^(a b c))

; Vanishing stuff via ghosts (empty packs) can only be observed via meta
; operations.  You cannot pass a ghost antiform to `=`
[
    ('~,~ = ^())
    ('~,~ = ^(comment "hi"))
    (^void = ^(void))

    (^void = ^(maybe void))
    (^void = ^ (maybe void))
    ~no-value~ !! (maybe comment "hi")

    ('~,~ = ^ ())
    ('~,~ = ^ (comment "hi"))
    (^void = ^ (void))
]

((the '10) = ^(10 comment "hi"))

(^null = ^(null))
('~[~null~]~ = ^(if ok [null]))

((the '1020) = ^(1000 + 20))
