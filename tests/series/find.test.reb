; functions/series/find.r
[blank? find blank 1]
[blank? find [] 1]
[
    blk: [1]
    same? blk find blk 1
]
[blank? find/part [x] 'x 0]
[equal? [x] find/part [x] 'x 1]
[equal? [x] find/reverse tail [x] 'x]
[equal? [y] find/match [x y] 'x]
[equal? [x] find/last [x] 'x]
[equal? [x] find/last [x x x] 'x]
; bug#66
[blank? find/skip [1 2 3 4 5 6] 2 3]
; bug#88
["c" = find "abc" charset ["c"]]
; bug#88
[blank? find/part "ab" "b" 1]
