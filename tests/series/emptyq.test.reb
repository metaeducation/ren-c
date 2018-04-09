; functions/series/emptyq.r
(empty? [])
(
    blk: tail of [1]
    clear head of blk
    empty? blk
)
(empty? blank)
[#190
    (x: copy "xx^/" loop 20 [enline x: join-of x x] true)
]
