; The ^GROUP! type is new and needs testing.

(meta-group! = type of '^(a b c))

; Vanishing stuff via ghosts (empty packs) can only be observed via meta
; operations.  You cannot pass a ghost antiform to `=`
[
    ('~,~ = meta ())
    ('~,~ = meta (comment "hi"))
    (void? ^(meta void))

    (void? ^(meta opt void))
    ((meta void) = ^ (opt void))
    ~no-value~ !! (opt comment "hi")

    ('~,~ = ^ ())
    ('~,~ = ^ (comment "hi"))
    ((meta void) = ^ (void))
]

((the '10) = meta (10 comment "hi"))

(null = ^(meta null))
('~[~null~]~ = meta (if ok [null]))

((the '1020) = meta (1000 + 20))
