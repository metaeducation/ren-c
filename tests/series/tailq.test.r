; functions/series/tailq.r
(tail? [])
(
    blk: tail of [1]
    clear head of blk
    not tail? blk  ; doesn't give range error, but doesn't say TAIL? either
)
