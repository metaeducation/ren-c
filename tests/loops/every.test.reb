; EVERY is similar to FOR-EACH but returns #[false] on any falsey body evals
; Still runs the body fully through for each value (assuming no BREAK)

(
    sum: 0
    did all [
        true = every x [1 3 7] [
            sum: me + x
            odd? x
        ]
        11 = sum
    ]
)

(
    sum: 0
    did all [
        null = every x [1 2 7] [
            sum: me + x
            odd? x
        ]
        10 = sum
    ]
)

(
    sum: 0
    did all [
        null = every x [1 2 7] [
            sum: me + x
            if even? x [break]
            true
        ]
        3 = sum
    ]
)

; !!! Loop protocol for EVERY is to let the last value fall through, but the
; CONTINUE throws a wrench as it would impede composition of EVERY if we
; expected it to just carry over the last value if those were two separate
; EVERY loops.
(
    sum: 0
    did all [
        '~ = ^ every x [1 2 7] [
            sum: me + x
            if x = 7 [continue]  ; Doesn't force NULL return, drops the 2
            x
        ]
        10 = sum
    ]
)

('~void~ = ^ every x [] [fail ~unreachable~])


('~ = ^ every x [1 2 3 4] [maybe if odd? x [x]])
(
    e: trap [every x [1 2 3 4] [if odd? x [x]]]
    e.id = 'bad-isotope
)

('~ = ^ every x [1 2 3 4] [comment "heavy"])
