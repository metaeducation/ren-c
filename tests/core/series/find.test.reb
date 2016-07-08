; functions/series/find.r
[blank? find blank 1]
[blank? find [] 1]
[
    blk: [1]
    same? blk find blk 1
]
; bug#66
[blank? find/skip [1 2 3 4 5 6] 2 3]
; bug#88
["c" = find "abc" charset ["c"]]
; bug#88
[blank? find/part "ab" "b" 1]
