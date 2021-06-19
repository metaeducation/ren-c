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
        false = every x [1 2 7] [
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

(
    sum: 0
    did all [
        false = every x [1 2 7] [
            sum: me + x
            if even? x [continue]  ; acts as `continue null`, get "falsified"
            true
        ]
        10 = sum
    ]
)

('~null~ = ^ every x [] [fail ~unreachable~])
