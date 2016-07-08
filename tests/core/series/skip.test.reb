; functions/series/skip.r
[
    blk: []
    same? blk skip blk 0
]
[
    blk: []
    same? blk skip blk 2147483647
]
[
    blk: []
    same? blk skip blk -1
]
[
    blk: []
    same? blk skip blk -2147483648
]
[
    blk: next [1 2 3]
    same? blk skip blk 0
]
[
    blk: next [1 2 3]
    equal? [3] skip blk 1
]
[
    blk: next [1 2 3]
    same? tail blk skip blk 2
]
[
    blk: next [1 2 3]
    same? tail blk skip blk 2147483647
]
[
    blk: at [1 2 3] 3
    same? tail blk skip blk 2147483646
]
[
    blk: at [1 2 3] 4
    same? tail blk skip blk 2147483645
]
[
    blk: [1 2 3]
    same? head blk skip blk -1
]
[
    blk: [1 2 3]
    same? head blk skip blk -2147483647
]
[
    blk: next [1 2 3]
    same? head blk skip blk -2147483648
]
