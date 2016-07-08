; functions/series/indexq.r
[1 == index? []]
[2 == index? next [a]]
; past-tail index
[
    a: tail copy [1]
    remove head a
    2 == index? a
]
; bug#1611: Allow INDEX? to take blank as an argument, return blank
[blank? index? blank]
