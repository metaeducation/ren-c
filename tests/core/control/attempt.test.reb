; functions/control/attempt.r
; bug#41
[blank? attempt [1 / 0]]
[1 = attempt [1]]
[void? attempt []]
; RETURN stops attempt evaluation
[
    f1: does [attempt [return 1 2] 2]
    1 == f1
]
; THROW stops attempt evaluation
[1 == catch [attempt [throw 1 2] 2]]
; BREAK stops attempt evaluation
[void? loop 1 [attempt [break 2] 2]]
[1 == loop 1 [attempt [break/return 1 2] 2]]
; recursion
[1 = attempt [attempt [1]]]
[blank? attempt [attempt [1 / 0]]]
; infinite recursion
[
    blk: [attempt blk]
    blank? attempt blk
]
