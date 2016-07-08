; functions/control/forall.r
[
    str: "abcdef"
    out: copy ""
    forall str [append out first str]
    all [
        head? str
        out = head str
    ]
]
[
    blk: [1 2 3 4]
    sum: 0
    forall blk [sum: sum + first blk]
    sum = 10
]
; cycle return value
[
    blk: [1 2 3 4]
    true = forall blk [true]
]
[
    blk: [1 2 3 4]
    false = forall blk [false]
]
; break cycle
[
    str: "abcdef"
    forall str [if #"c" = char: str/1 [break]]
    char = #"c"
]
; break return value
[
    blk: [1 2 3 4]
    void? forall blk [break]
]
; break/return return value
[
    blk: [1 2 3 4]
    1 = forall blk [break/return 1]
]
; continue cycle
[
    success: true
    x: "a"
    forall x [continue success: false]
    success
]
; zero repetition
[
    success: true
    blk: []
    forall blk [success: false]
    success
]
; Test that return stops the loop
[
    blk: [1]
    f1: does [forall blk [return 1 2]]
    1 = f1
]
; Test that errors do not stop the loop and errors can be returned
[
    num: 0
    blk: [1 2]
    e: forall blk [num: first blk try [1 / 0]]
    all [error? e num = 2]
]
; recursivity
[
    num: 0
    blk1: [1 2 3 4 5]
    blk2: [6 7]
    forall blk1 [
        num: num + first blk1
        forall blk2 [num: num + first blk2]
    ]
    num = 80
]
; bug#81
[
    blk: [1]
    1 == forall blk [blk/1]
]
