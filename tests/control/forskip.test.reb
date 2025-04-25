; functions/control/for-skip.r
(
    blk: copy out: copy []
    for i 1 25 1 [append blk i]
    iterate-skip blk 3 [append out blk/1]
    out = [1 4 7 10 13 16 19 22 25]
)
; cycle return value
(
    blk: [1 2 3 4]
    okay = iterate-skip blk 1 [okay]
)
(
    blk: [1 2 3 4]
    'foo = iterate-skip blk 1 ['foo]
)
; break cycle
(
    str: "abcdef"
    iterate-skip str 2 [
        if #"c" = char: str/1 [break]
    ]
    char = #"c"
)
; break return value
(
    blk: [1 2 3 4]
    null? iterate-skip blk 2 [break]
)
; continue cycle
(
    success: okay
    x: "a"
    iterate-skip x 1 [continue success: null]
    success
)
; zero repetition
(
    success: okay
    blk: []
    iterate-skip blk 1 [success: null]
    success
)
; Test that return stops the loop
(
    blk: [1]
    f1: func [] [iterate-skip blk 2 [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    blk: [1 2]
    e: iterate-skip blk 1 [num: first blk sys/util/rescue [1 / 0]]
    all [error? e num = 2]
)
; recursivity
(
    num: 0
    blk1: [1 2 3 4 5]
    blk2: [6 7]
    iterate-skip blk1 1 [
        num: num + first blk1
        iterate-skip blk2 1 [num: num + first blk2]
    ]
    num = 80
)
