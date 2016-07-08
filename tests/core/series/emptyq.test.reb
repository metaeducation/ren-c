; functions/series/emptyq.r
[empty? []]
[
    blk: tail [1]
    clear head blk
    empty? blk
]
[empty? blank]
; bug#190
[x: copy "xx^/" loop 20 [enline x: join x x] true]
