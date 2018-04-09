; functions/control/for.r
(
    success: true
    num: 0
    for i 1 10 1 [
        num: num + 1
        success: success and (i = num)
    ]
    success and (10 = num)
)
; cycle return value
(bar? for i 1 1 1 [false])
; break cycle
(
    num: 0
    for i 1 10 1 [num: i break]
    num = 1
)
; break return value
(blank? for i 1 10 1 [break])

; continue cycle
[#58 (
    success: true
    for i 1 1 1 [continue success: false]
    success
)]
(
    success: true
    x: "a"
    for i x tail of x 1 [continue success: false]
    success
)
; string! test
(
    out: copy ""
    for i s: "abc" back tail of s 1 [append out i]
    out = "abcbcc"
)
; block! test
(
    out: copy []
    for i b: [1 2 3] back tail of b 1 [append out i]
    out = [1 2 3 2 3 3]
)
; zero as end target, infinite loop unless i is changed, or interrupted
(
    count: 0
    for i 1 0 1 [
        count: me + 1
        if count > 1023 [
            break ;-- close enough to infinite :-) assume infinite loop
        ]
    ]
    count = 1024
)
; zero repetition block test
(
    success: true
    for i b: [1] tail of :b -1 [success: false]
    success
)
; Test that return stops the loop
(
    f1: does [for i 1 1 1 [return 1 2] 2]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: for i 1 2 1 [num: i try [1 / 0]]
    all [error? e num = 2]
)
; infinite loop tests
(
    num: 0
    for i b: [1] tail of b 1 [
        num: num + 1
        if num > 2 [break]
    ]
    num <= 2
)
(
    num: 0
    for i 2147483647 2147483647 1 [
        num: num + 1
        either num > 1 [break] [true]
    ]
)
(
    num: 0
    for i -2147483648 -2147483648 -1 [
        num: num + 1
        either num > 1 [break] [true]
    ]
)
<64bit>
[#1136 (
    num: 0
    for i 9223372036854775807 9223372036854775807 -9223372036854775808 [
        num: num + 1
        if num <> 1 [break]
        true
    ]
)]
<64bit>
(
    num: 0
    for i -9223372036854775808 -9223372036854775808 9223372036854775807 [
        num: num + 1
        if num <> 1 [break]
        true
    ]
)
(
    num: 0
    for i 2147483647 2147483647 2147483647 [
        num: num + 1
        if num <> 1 [break]
        true
    ]
)
(
    num: 0
    for i 2147483647 2147483647 -2147483648 [
        num: num + 1
        if num <> 1 [break]
        true
    ]
)
(
    num: 0
    for i -2147483648 -2147483648 2147483647 [
        num: num + 1
        if num <> 1 [break]
        true
    ]
)
(
    num: 0
    for i -2147483648 -2147483648 -2147483648 [
        num: num + 1
        if num <> 1 [break]
        true
    ]
)
[#1993 (
    equal?
        type of for i -1 -2 0 [break]
        type of for i 2 1 0 [break]
)]
; skip before head test
([] = for i b: tail of [1] head of b -2 [i])
; "recursive safety", "locality" and "body constantness" test in one
(for i 1 1 1 b: [not same? 'i b/3])
; recursivity
(
    num: 0
    for i 1 5 1 [
        for i 1 2 1 [num: num + 1]
    ]
    num = 10
)
; infinite recursion
(
    blk: [for i 1 1 1 blk]
    error? try blk
)
; local variable changeability - this is how it works in R3
(
    test: false
    blank? for i 1 3 1 [
        if i = 2 [
            if test [break]
            test: true
            i: 1
        ]
    ]
)
; local variable type safety
(
    test: false
    error? try [
        for i 1 2 [
            either test [i == 2] [
                test: true
                i: false
            ]
        ]
    ]
)
; FOR should not bind 'self
[#1529
    (same? 'self for i 1 1 1 ['self])
]

[#1136 (
    e: trap [
        num: 0
        for i 9223372036854775806 9223372036854775807 2 [
            num: num + 1
            either num > 1 [break] [true]
        ]
    ]
    error? e and (e/id = 'overflow)
)]
(
    e: trap [
        num: 0
        for i -9223372036854775807 -9223372036854775808 -2 [
            num: num + 1
            either num > 1 [break] [true]
        ]
    ]
    error? e and (e/id = 'overflow)
)

[#1994 (
    e: trap [
        num: 0
        for i 9223372036854775806 9223372036854775807 9223372036854775807 [
            num: num + 1
            if num <> 1 [break]
            true
        ]
    ]
    error? e and (e/id = 'overflow)
)]
(
    e: trap [
        num: 0
        for i -9223372036854775807 -9223372036854775808 -9223372036854775808 [
            num: num + 1
            if num <> 1 [break]
            true
        ]
    ]
    error? e and (e/id = 'overflow)
)

[#1993
    (equal? (type of for i 1 2 0 [break]) (type of for i 2 1 0 [break]))
]
(equal? (type of for i -1 -2 0 [break]) (type of for i -2 -1 0 [break]))
