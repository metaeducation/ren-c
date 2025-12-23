; The ^GROUP! type is new and needs testing.

(metaform! = type of '^(a b c))

; Vanishing stuff via voids can only be observed via meta operations.
; You cannot pass a void antiform to `=`
[
    ('~,~ = lift ())
    ('~,~ = lift (comment "hi"))
    (ghost? unlift (lift ^ghost))

    (ghost? unlift (lift opt ^ghost))
    ((lift ^ghost) = lift (opt ^ghost))
    ~no-value~ !! (opt comment "hi")

    ('~,~ = lift ())
    ('~,~ = lift (comment "hi"))
    ((lift ^ghost) = lift (^ghost))
]

((the '10) = lift (10 comment "hi"))

(null = unlift (lift null))
('~[~null~]~ = lift (if ok [null]))

((the '1020) = lift (1000 + 20))
