; functions/series/indexq.r
(1 == index of [])
(2 == index of next [a])
; past-tail index
(
    a: tail of copy [1]
    remove head of a
    2 == index of a
)
[#1611
    ~type-has-no-index~ !! (index of blank)
    (null? try index of blank)
]
