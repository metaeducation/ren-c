; EVERY is similar to FOR-EACH but returns ~null~ antiform on any null body
; evals.  Still runs the body fully through for each value (assuming no BREAK)

(
    sum: 0
    all [
        okay = every x [1 3 7] [
            sum: me + x
            odd? x
        ]
        11 = sum
    ]
)

(
    sum: 0
    all [
        null = every x [1 2 7] [
            sum: me + x
            odd? x
        ]
        10 = sum
    ]
)

(
    sum: 0
    all [
        null = every x [1 2 7] [
            sum: me + x
            if even? x [break]
            okay
        ]
        3 = sum
    ]
)

(
    sum: 0
    all [
        okay = every x [1 2 7] [
            sum: me + x
            if even? x [continue]  ; acts as `continue void`, ignored
            okay
        ]
        10 = sum
    ]
)

(void? every x [] [<unused>])
