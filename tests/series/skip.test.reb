; functions/series/skip.r

; Normal invocation: clips to series bounds
(
    blk: []
    same? blk skip blk 0
)
(
    blk: []
    same? blk skip blk 2147483647
)
(
    blk: []
    same? blk skip blk -1
)
(
    blk: []
    same? blk skip blk -2147483648
)
(
    blk: next [1 2 3]
    same? blk skip blk 0
)
(
    blk: next [1 2 3]
    [3] == skip blk 1
)
(
    blk: next [1 2 3]
    same? tail of blk skip blk 2
)
(
    blk: next [1 2 3]
    same? tail of blk skip blk 2147483647
)
(
    blk: at [1 2 3] 3
    same? tail of blk skip blk 2147483646
)
(
    blk: at [1 2 3] 4
    same? tail of blk skip blk 2147483645
)
(
    blk: [1 2 3]
    same? head of blk skip blk -1
)
(
    blk: [1 2 3]
    same? head of blk skip blk -2147483647
)
(
    blk: next [1 2 3]
    same? head of blk skip blk -2147483648
)


; /ONLY invocation: returns BLANK if out of bounds
(
    blk: []
    same? blk skip* blk 0
)
(
    blk: []
    null? skip* blk 2147483647
)
(
    blk: []
    null? skip* blk -1
)
(
    blk: []
    null? skip* blk -2147483648
)
(
    blk: next [1 2 3]
    same? blk skip* blk 0
)
(
    blk: next [1 2 3]
    [3] == skip* blk 1
)
(
    blk: next [1 2 3]
    same? tail of blk skip* blk 2
)
(
    blk: next [1 2 3]
    null? skip* blk 2147483647
)
(
    blk: at [1 2 3] 3
    null? skip* blk 2147483646
)
(
    blk: at [1 2 3] 4
    null? skip* blk 2147483645
)
(
    blk: [1 2 3]
    null? skip* blk -1
)
(
    blk: [1 2 3]
    null? skip* blk -2147483647
)
(
    blk: next [1 2 3]
    null? skip* blk -2147483648
)
