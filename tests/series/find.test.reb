; functions/series/find.r
[#473 (
    null? find void 1
)]
(null? find [] 1)
(
    blk: [1]
    same? blk find blk 1
)
(null? find/part [x] 'x 0)
(equal? [x] find/part [x] 'x 1)
(equal? [x] find/reverse tail of [x] 'x)
(equal? [y] find/match [x y] 'x)
(equal? [x] find/last [x] 'x)
(equal? [x] find/last [x x x] 'x)
[#66
    (null? find/skip [1 2 3 4 5 6] 2 3)
]
[#88
    ("c" = find "abc" charset ["c"])
]
[#88
    (null? find/part "ab" "b" 1)
]

[#2324 (
    str: "1.1.1"
    all [
        "1.1.1" = find/part str "1." 2
        (elide str: skip str 2)
        "1.1" = find str "1."
        "1.1" = find/part str "1." 2
    ]
)]
