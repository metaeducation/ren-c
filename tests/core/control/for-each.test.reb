; functions/control/for-each.r
[
    out: copy ""
    str: "abcdef"
    for-each i str [append out i]
    out = str
]
[
    blk: [1 2 3 4]
    sum: 0
    for-each i blk [sum: sum + i]
    sum = 10
]
; cycle return value
[
    blk: [1 2 3 4]
    true = for-each i blk [true]
]
[
    blk: [1 2 3 4]
    false = for-each i blk [false]
]
; break cycle
[
    str: "abcdef"
    for-each i str [
        num: i
        if i = #"c" [break]
    ]
    num = #"c"
]
; break return value
[
    blk: [1 2 3 4]
    void? for-each i blk [break]
]
; break/return return value
[
    blk: [1 2 3 4]
    1 = for-each i blk [break/return 1]
]
; continue cycle
[
    success: true
    for-each i [1] [continue success: false]
    success
]
; zero repetition
[
    success: true
    blk: []
    for-each i blk [success: false]
    success
]
; Test that return stops the loop
[
    blk: [1]
    f1: does [for-each i blk [return 1 2]]
    1 = f1
]
; Test that errors do not stop the loop and errors can be returned
[
    num: 0
    blk: [1 2]
    e: for-each i blk [num: i try [1 / 0]]
    all [error? e num = 2]
]
; "recursive safety", "locality" and "body constantness" test in one
[for-each i [1] b: [not same? 'i b/3]]
; recursivity
[
    num: 0
    for-each i [1 2 3 4 5] [
        for-each i [1 2] [num: num + 1]
    ]
    num = 10
]
