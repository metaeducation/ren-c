; functions/math/random.r
; bug#1084
[
    random/seed 0
    not any [
        negative? random 1.0
        negative? random 1.0
    ]
]
; bug#1875
[
    random/seed 0
    2 = random/only next [1 2]
]
; bug#932
[
    s: "aa"
    random/seed s
    a: random 10000
    random/seed s
    a = random 10000
]
