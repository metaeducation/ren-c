; functions/series/find.r
[#473 (
    null? find blank 1
)]
(null? find [] 1)
(
    blk: [1]
    same? blk find blk 1
)
(null? find/part [x] 'x 0)
([x] == find/part [x] 'x 1)
([x] == find/reverse tail of [x] 'x)
([y] == find/match [x y] 'x)
([x] == find/last [x] 'x)
([x] == find/last [x x x] 'x)
[#66
    (null? find/skip [1 2 3 4 5 6] 2 3)
]
[#88
    ("c" == find "abc" charset ["c"])
]
[#88
    (null? find/part "ab" "b" 1)
]
