; functions/series/at.r
(
    blk: []
    same? blk at blk 1
)
(
    blk: []
    not same? blk at blk 2147483647
)
(
    blk: []
    same? blk at blk 0
)
(
    blk: []
    not same? blk at blk -1
)
(
    blk: []
    not same? blk at blk -2147483648
)
(
    blk: tail of [1 2 3]
    same? blk at blk 1
)
(
    blk: tail of [1 2 3]
    same? blk at blk 0
)
(
    blk: tail of [1 2 3]
    equal? [3] at blk -1
)
(
    blk: tail of [1 2]
    not same? blk at blk 2147483647
)
(
    blk: [1 2]
    not same? blk at blk -2147483647
)
(
    blk: [1 2]
    not same? blk at blk -2147483648
)

; string
(
    str: ""
    same? str at str 1
)
(
    str: tail of "123"
    same? str at str 1
)
(
    str: ""
    same? str at str 0  ; !!! currently AT STR 0 and AT STR 1 same, review
)
(
    str: tail of "123"
    same? str at str 0
)

(
    str: ""
    not same? str at str 2147483647
)
(
    str: ""
    not same? str at str -1
)
(
    str: ""
    not same? str at str -2147483648
)
(
    str: tail of "123"
    equal? "3" at str -1
)
(
    str: tail of "12"
    not same? str at str 2147483647
)
(
    str: "12"
    not same? str at str -2147483647
)
(
    str: "12"
    not same? str at str -2147483648
)
