; functions/control/for-next.r
(
    str: "abcdef"
    out: copy ""
    for-next str [append out first str]
    all [
        head? str
        out = head of str
    ]
)
(
    blk: [1 2 3 4]
    sum: 0
    for-next blk [sum: sum + first blk]
    sum = 10
)
; cycle return value
(
    blk: [1 2 3 4]
    true = for-next blk [true]
)
(
    blk: [1 2 3 4]
    false = for-next blk [false]
)
; break cycle
(
    str: "abcdef"
    for-next str [if #"c" = char: str/1 [break]]
    char = #"c"
)
; break return value
(
    blk: [1 2 3 4]
    null? for-next blk [break]
)
; continue cycle
(
    success: true
    x: "a"
    for-next x [continue success: false]
    success
)
; zero repetition
(
    success: true
    blk: []
    for-next blk [success: false]
    success
)
; Test that return stops the loop
(
    blk: [1]
    f1: func [] [for-next blk [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    blk: [1 2]
    e: for-next blk [num: first blk trap [1 / 0]]
    all [error? e num = 2]
)
; recursivity
(
    num: 0
    blk1: [1 2 3 4 5]
    blk2: [6 7]
    for-next blk1 [
        num: num + first blk1
        for-next blk2 [num: num + first blk2]
    ]
    num = 80
)
[#81 (
    blk: [1]
    1 == for-next blk [blk/1]
)]
