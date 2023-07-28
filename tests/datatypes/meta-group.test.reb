; The META-GROUP! type is new and needs testing.

(meta-group! = kind of '^(a b c))

; Voids don't vanish in ordinary DO operations (just special constructs which
; choose to interpret them as vanishing).  But they are invalid as parameters
; to things like EQUAL?, so meta operations (or functions taking meta args)
; are the only way to test for and process them.
;
; Actual vanishing stuff via nihil (empty packs) can only be observed via
; meta operations.
[
    (nihil' = ^())
    (nihil' = ^(comment "hi"))
    (void' = ^(void))

    (void' = ^(maybe void))
    (void' = ^ (maybe void))
    ~no-value~ !! (maybe comment "hi")

    (nihil' = ^ ())
    (nihil' = ^ (comment "hi"))
    (void' = ^ (void))
]

((the '10) = ^(10 comment "hi"))

(null' = ^(null))
('~[~null~]~ = ^(if true [null]))

((the '1020) = ^(1000 + 20))
