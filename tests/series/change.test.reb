; functions/series/change.r
(
    blk1: at copy [1 2 3 4 5] 3
    blk2: at copy [1 2 3 4 5] 3
    change/part blk1 6 -2147483647
    change/part blk2 6 -2147483648
    equal? head of blk1 head of blk2
)
[#9
    (equal? "tr" change/part "str" "" 1)
]
