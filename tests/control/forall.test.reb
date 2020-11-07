; functions/control/for-next.r
(
    str: "abcdef"
    out: copy ""
    iterate str [append out first str]
    all [
        head? str
        out = head of str
    ]
)
(
    blk: [1 2 3 4]
    sum: 0
    iterate blk [sum: sum + first blk]
    sum = 10
)
; cycle return value
(
    blk: [1 2 3 4]
    true = iterate blk [true]
)
(
    blk: [1 2 3 4]
    false = iterate blk [false]
)
; break cycle
(
    str: "abcdef"
    iterate str [if #"c" = char: str/1 [break]]
    char = #"c"
)
; break return value
(
    blk: [1 2 3 4]
    null? iterate blk [break]
)
; continue cycle
(
    success: true
    x: "a"
    iterate x [continue, success: false]
    success
)
; zero repetition
(
    success: true
    blk: []
    iterate blk [success: false]
    success
)
; Test that return stops the loop
(
    blk: [1]
    f1: func [] [iterate blk [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    blk: [1 2]
    e: iterate blk [num: first blk trap [1 / 0]]
    all [error? e num = 2]
)
; recursivity
(
    num: 0
    blk1: [1 2 3 4 5]
    blk2: [6 7]
    iterate blk1 [
        num: num + first blk1
        iterate blk2 [num: num + first blk2]
    ]
    num = 80
)
[#81 (
    blk: [1]
    1 == iterate blk [blk/1]
)]
