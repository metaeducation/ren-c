; functions/series/back.r
[
    a: [1]
    same? a back a
]
[
    a: tail [1]
    same? head a back a
]
; path
[
    a: 'b/c
    same? a back a
]
[
    a: tail 'b/c
    same? head a back back a
]
; string
[
    a: tail "1"
    same? head a back a
]
[
    a: "1"
    same? a back a
]
