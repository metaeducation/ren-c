; %loops/iterate-skip.test.r

(
    blk: copy out: copy []
    cfor 'i 1 25 1 [append blk i]
    iterate-skip @blk 3 [append out blk.1]
    out = [1 4 7 10 13 16 19 22 25]
)
; cycle return value
(
    blk: [1 2 3 4]
    'true = iterate-skip @blk 1 ['true]
)
(
    blk: [1 2 3 4]
    'false = iterate-skip @blk 1 ['false]
)
; break cycle
(
    str: "abcdef"
    char: ~
    iterate-skip @str 2 [
        if #c = char: str.1 [break]
    ]
    char = #c
)
; break return value
(
    blk: [1 2 3 4]
    null? iterate-skip @blk 2 [break]
)
; continue cycle
(
    success: 'true
    x: "a"
    iterate-skip @x 1 [continue, success: 'false]
    true? success
)
; zero repetition
(
    success: 'true
    blk: []
    iterate-skip @blk 1 [success: 'false]
    true? success
)
; Test that return stops the loop
(
    blk: [1]
    f1: func [return: [integer!]] [iterate-skip @blk 2 [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    blk: [1 2]
    e: iterate-skip @blk 1 [num: first blk rescue [1 / 0]]
    all [warning? e num = 2]
)
; recursivity
(
    num: 0
    blk1: [1 2 3 4 5]
    blk2: [6 7]
    iterate-skip @blk1 1 [
        num: num + first blk1
        iterate-skip @blk2 1 [num: num + first blk2]
    ]
    num = 80
)
