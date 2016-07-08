; functions/control/forskip.r
[
    blk: copy out: copy []
    for i 1 25 1 [append blk i]
    forskip blk 3 [append out blk/1]
    out = [1 4 7 10 13 16 19 22 25]
]
; cycle return value
[
    blk: [1 2 3 4]
    true = forskip blk 1 [true]
]
[
    blk: [1 2 3 4]
    false = forskip blk 1 [false]
]
; break cycle
[
    str: "abcdef"
    forskip str 2 [if #"c" = char: str/1 [break]
    ]
    char = #"c"
]
; break return value
[
    blk: [1 2 3 4]
    void? forskip blk 2 [break]
]
; break/return return value
[
    blk: [1 2 3 4]
    1 = forskip blk 2 [break/return 1]
]
; continue cycle
[
    success: true
    x: "a"
    forskip x 1 [continue success: false]
    success
]
; zero repetition
[
    success: true
    blk: []
    forskip blk 1 [success: false]
    success
]
; Test that return stops the loop
[
    blk: [1]
    f1: does [forskip blk 2 [return 1 2]]
    1 = f1
]
; Test that errors do not stop the loop and errors can be returned
[
    num: 0
    blk: [1 2]
    e: forskip blk 1 [num: first blk try [1 / 0]]
    all [error? e num = 2]
]
; recursivity
[
    num: 0
    blk1: [1 2 3 4 5]
    blk2: [6 7]
    forskip blk1 1 [
        num: num + first blk1
        forskip blk2 1 [num: num + first blk2]
    ]
    num = 80
]
