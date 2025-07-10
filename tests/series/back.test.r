; functions/series/back.r
(
    a: [1]
    null? back a
)
(
    a: tail of [1]
    same? head of a back a
)
; string
(
    a: tail of "1"
    same? head of a back a
)
(
    a: "1"
    null? back a
)
