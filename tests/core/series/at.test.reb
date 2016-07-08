; functions/series/at.r
[
    blk: []
    same? blk at blk 1
]
[
    blk: []
    same? blk at blk 2147483647
]
[
    blk: []
    same? blk at blk 0
]
[
    blk: []
    same? blk at blk -1
]
[
    blk: []
    same? blk at blk -2147483648
]
[
    blk: tail [1 2 3]
    same? blk at blk 1
]
[
    blk: tail [1 2 3]
    same? blk at blk 0
]
[
    blk: tail [1 2 3]
    equal? [3] at blk -1
]
[
    blk: tail [1 2]
    same? blk at blk 2147483647
]
[
    blk: [1 2]
    same? blk at blk -2147483647
]
[
    blk: [1 2]
    same? blk at blk -2147483648
]
; string
[
    str: ""
    same? str at str 1
]
[
    str: ""
    same? str at str 2147483647
]
[
    str: ""
    same? str at str 0
]
[
    str: ""
    same? str at str -1
]
[
    str: ""
    same? str at str -2147483648
]
[
    str: tail "123"
    same? str at str 1
]
[
    str: tail "123"
    same? str at str 0
]
[
    str: tail "123"
    equal? "3" at str -1
]
[
    str: tail "12"
    same? str at str 2147483647
]
[
    str: "12"
    same? str at str -2147483647
]
[
    str: "12"
    same? str at str -2147483648
]
