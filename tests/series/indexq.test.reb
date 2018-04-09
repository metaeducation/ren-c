; functions/series/indexq.r
(1 == index? [])
(2 == index? next [a])
; past-tail index
(
    a: tail of copy [1]
    remove head of a
    2 == index? a
)
[#1611 ; Allow INDEX? to take blank as an argument, return blank
    (blank? index? blank)
]
