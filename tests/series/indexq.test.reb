; functions/series/indexq.r
(1 == index? [])
(2 == index? next [a])
; past-tail index
(
    a: tail of copy [1]
    remove head of a
    2 == index? a
)
[#1611
    (null? index of void)
]
