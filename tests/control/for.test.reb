; functions/control/for.r

; One design aspect of FOR which was introduced in Ren-C is the idea that
; it should not be possible to give a start/end/bump combination that in and
; of itself will cause an infinite loop.  This is accomplished by determining
; the direction of iteration based on comparing the start and end points...
; not the sign of the bump value (as Rebol2 and R3-Alpha did).
;
; The proposal originated from @BrianH on CureCode:
;
; https://github.com/rebol/rebol-issues/issues/1993

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
(false = for i 1 1 1 [false])
; break cycle
(
    num: 0
    for i 1 10 1 [num: i break]
    num = 1
)
; break return value
(null? for i 1 10 1 [break])

; continue cycle
[#58 (
    success: true
    for i 1 1 1 [continue, success: false]
    success
)]
(
    success: true
    x: "a"
    for i x tail of x 1 [continue, success: false]
    success
)
; text! test
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
; zero repetition block test
(
    success: true
    for i b: [1] tail of :b -1 [success: false]
    success
)
; Test that return stops the loop
(
    f1: func [] [for i 1 1 1 [return 1 2] 2]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: for i 1 2 1 [num: i trap [1 / 0]]
    all [error? e num = 2]
)

[ ; infinite loop tests
    (
        num: 0
        for i (b: next [1]) (back b) 1 [
            num: num + 1
            break
        ]
        num = 0
    )(
        num: 0
        for i 1 0 1 [
            num: num + 1
            break
        ]
        num = 0
    )(
        num: 0
        for i 0 1 -1 [
            num: num + 1
            break
        ]
        num = 0
    )
]

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
    error? trap blk
)
; local variable changeability - this is how it works in R3
(
    test: false
    null? for i 1 3 1 [
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
    error? trap [
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
    e/id = 'overflow
)]
(
    e: trap [
        num: 0
        for i -9223372036854775807 -9223372036854775808 -2 [
            num: num + 1
            either num > 1 [break] [true]
        ]
    ]
    e/id = 'overflow
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
    e/id = 'overflow
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
    e/id = 'overflow
)

[#1993
    (equal? (type of for i 1 2 0 [break]) (type of for i 2 1 0 [break]))
]
(equal? (type of for i -1 -2 0 [break]) (type of for i -2 -1 0 [break]))
