; functions/series/find.r
[#473 (
    blank? find blank 1
)]
(blank? find [] 1)
(
    blk: [1]
    same? blk find blk 1
)
(blank? find/part [x] 'x 0)
(equal? [x] find/part [x] 'x 1)
(equal? [x] find/reverse tail of [x] 'x)
(equal? [y] find/match [x y] 'x)
(equal? [x] find/last [x] 'x)
(equal? [x] find/last [x x x] 'x)
[#66
    (blank? find/skip [1 2 3 4 5 6] 2 3)
]
[#88
    ("c" = find "abc" charset ["c"])
]
[#88
    (blank? find/part "ab" "b" 1)
]
