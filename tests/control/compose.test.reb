; functions/control/compose.r
(
    num: 1
    [1 num] = compose [(num) num]
)
([] = compose [])
(
    blk: []
    append blk [try [1 / 0]]
    blk = compose blk
)
; RETURN stops the evaluation
(
    f1: does [compose [(return 1)] 2]
    1 = f1
)
; THROW stops the evaluation
(1 = catch [compose [(throw 1 2)] 2])
; BREAK stops the evaluation
(blank? loop 1 [compose [(break 2)] 2])
; Test that errors do not stop the evaluation:
(block? compose [(try [1 / 0])])
(
    blk: []
    not same? blk compose blk
)
(
    blk: [[]]
    same? first blk first compose blk
)
(
    blk: []
    same? blk first compose [(reduce [blk])]
)
(
    blk: []
    same? blk first compose/only [(blk)]
)
; recursion
(
    num: 1
    [num 1] = compose [num (compose [(num)])]
)
; infinite recursion
(
    blk: [(compose blk)]
    error? try blk
)

; #1906
(
    b: copy [] insert/dup b 1 32768 compose b
    sum: 0
    for-each i b [sum: me + i]
    sum = 32768
)
