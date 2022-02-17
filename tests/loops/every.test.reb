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
; EVERY loops.  Review.
(
    sum: 0
    did all [
        '~none~ = ^ every x [1 2 7] [
            sum: me + x
            if x = 7 [continue]  ; acts as `continue ~none~` doesn't keep old
            x
        ]
        10 = sum
    ]
)

('~none~ = ^ every x [] [fail ~unreachable~])
