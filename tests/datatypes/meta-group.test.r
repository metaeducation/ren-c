; The ^GROUP! type is new and needs testing.

(meta-group! = type of '^(a b c))

; Vanishing stuff via ghosts (empty packs) can only be observed via meta
; operations.  You cannot pass a ghost antiform to `=`
[
    ('~,~ = lift ())
    ('~,~ = lift (comment "hi"))
    (void? unlift (lift ^void))

    (void? unlift (lift opt ^void))
    ((lift ^void) = ^ (opt ^void))
    ~no-value~ !! (opt comment "hi")

    ('~,~ = ^ ())
    ('~,~ = ^ (comment "hi"))
    ((lift ^void) = lift (^void))
]

((the '10) = lift (10 comment "hi"))

(null = unlift (lift null))
('~[~null~]~ = lift (if ok [null]))

((the '1020) = lift (1000 + 20))
