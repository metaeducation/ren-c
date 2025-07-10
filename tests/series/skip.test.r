; functions/series/skip.r

; Normal invocation: clips to series bounds
(
    blk: []
    same? blk skip:unbounded blk 0
)
(
    blk: []
    not same? blk skip:unbounded blk 2147483647
)
(
    blk: []
    not same? blk skip:unbounded blk -1
)
(
    blk: []
    not same? blk skip:unbounded blk -2147483648
)
(
    blk: next [1 2 3]
    same? blk skip:unbounded blk 0
)
(
    blk: next [1 2 3]
    equal? [3] skip:unbounded blk 1
)
(
    blk: next [1 2 3]
    same? tail of blk skip:unbounded blk 2
)
(
    blk: next [1 2 3]
    not same? tail of blk skip:unbounded blk 2147483647
)
(
    blk: at [1 2 3] 3
    not same? tail of blk skip:unbounded blk 2147483646
)
(
    blk: at [1 2 3] 4
    not same? tail of blk skip:unbounded blk 2147483645
)
(
    blk: [1 2 3]
    not same? head of blk skip:unbounded blk -1
)
(
    blk: [1 2 3]
    not same? head of blk skip:unbounded blk -2147483647
)
(
    blk: next [1 2 3]
    not same? head of blk skip:unbounded blk -2147483648
)


; non-:UNBOUNDED (returns NULL if out of bounds)
(
    blk: []
    same? blk skip blk 0
)
(
    blk: []
    null? skip blk 2147483647
)
(
    blk: []
    null? skip blk -1
)
(
    blk: []
    null? skip blk -2147483648
)
(
    blk: next [1 2 3]
    same? blk skip blk 0
)
(
    blk: next [1 2 3]
    equal? [3] skip blk 1
)
(
    blk: next [1 2 3]
    same? tail of blk skip blk 2
)
(
    blk: next [1 2 3]
    null? skip blk 2147483647
)
(
    blk: at [1 2 3] 3
    null? skip blk 2147483646
)
(
    blk: at [1 2 3] 4
    null? skip blk 2147483645
)
(
    blk: [1 2 3]
    null? skip blk -1
)
(
    blk: [1 2 3]
    null? skip blk -2147483647
)
(
    blk: next [1 2 3]
    null? skip blk -2147483648
)
