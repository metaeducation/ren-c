; %loops/cfor.r

; One design aspect of CFOR which was introduced in Ren-C is the idea that
; it should not be possible to give a start/end/bump combination that in and
; of itself will cause an infinite loop.  This is accomplished by determining
; the direction of iteration based on comparing the start and end points...
; not the sign of the bump value (as Rebol2 and R3-Alpha did).
;
; The proposal originated from @BrianH on CureCode:
;
; https://github.com/rebol/rebol-issues/issues/1993

(
    success: 'true
    num: 0
    cfor 'i 1 10 1 [
        num: num + 1
        success: boolean (true? success) and (i = num)
    ]
    (true? success) and (10 = num)
)
; cycle return value
('false = cfor 'i 1 1 1 ['false])
; break cycle
(
    num: 0
    cfor 'i 1 10 1 [num: i break]
    num = 1
)
; break return value
(null? cfor 'i 1 10 1 [break])

; continue cycle
[#58 (
    success: 'true
    cfor 'i 1 1 1 [continue, success: 'false]
    true? success
)]
(
    success: 'true
    x: "a"
    cfor 'i x tail of x 1 [continue, success: 'false]
    true? success
)
; text! test
(
    out: copy ""
    cfor 'i s: "abc" back tail of s 1 [append out i]
    out = "abcbcc"
)
; block! test
(
    out: copy []
    cfor 'i b: [1 2 3] back tail of b 1 [append out spread i]
    out = [1 2 3 2 3 3]
)
; zero repetition block test
(
    success: 'true
    cfor 'i b: [1] tail of :b -1 [success: 'false]
    true? success
)
; Test that return stops the loop
(
    f1: func [return: [integer!]] [cfor 'i 1 1 1 [return 1 2] 2]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: cfor 'i 1 2 1 [num: i rescue [1 / 0]]
    all [warning? e num = 2]
)

[ ; infinite loop tests
    (
        num: 0
        b: ~
        cfor 'i (b: next [1]) (back b) 1 [
            num: num + 1
            break
        ]
        num = 0
    )(
        num: 0
        cfor 'i 1 0 1 [
            num: num + 1
            break
        ]
        num = 0
    )(
        num: 0
        cfor 'i 0 1 -1 [
            num: num + 1
            break
        ]
        num = 0
    )
]

(
    num: 0
    cfor 'i 2147483647 2147483647 1 [
        num: num + 1
        either num > 1 [break] [okay]
    ]
)
(
    num: 0
    cfor 'i -2147483648 -2147483648 -1 [
        num: num + 1
        either num > 1 [break] [okay]
    ]
)
<64bit>
[#1136 (
    num: 0
    cfor 'i 9223372036854775807 9223372036854775807 -9223372036854775808 [
        num: num + 1
        if num <> 1 [break]
        ok
    ]
)]
<64bit>
(
    num: 0
    cfor 'i -9223372036854775808 -9223372036854775808 9223372036854775807 [
        num: num + 1
        if num <> 1 [break]
        ok
    ]
)
(
    num: 0
    cfor 'i 2147483647 2147483647 2147483647 [
        num: num + 1
        if num <> 1 [break]
        ok
    ]
)
(
    num: 0
    cfor 'i 2147483647 2147483647 -2147483648 [
        num: num + 1
        if num <> 1 [break]
        ok
    ]
)
(
    num: 0
    cfor 'i -2147483648 -2147483648 2147483647 [
        num: num + 1
        if num <> 1 [break]
        ok
    ]
)
(
    num: 0
    cfor 'i -2147483648 -2147483648 -2147483648 [
        num: num + 1
        if num <> 1 [break]
        ok
    ]
)
[#1993
    (null? cfor 'i -1 -2 0 [break])
    (null? cfor 'i -2 -1 0 [break])
    (null? cfor 'i 2 1 0 [break])
    (null? cfor 'i 1 2 0 [break])
]
; skip before head test
([] = cfor 'i b: tail of [1] head of b -2 [i])

; "recursive safety", "locality" and "body constantness" test in one
(cfor 'i 1 1 1 b: [not same? 'i b.3])

; recursivity
(
    num: 0
    cfor 'i 1 5 1 [
        cfor 'i 1 2 1 [num: num + 1]
    ]
    num = 10
)
; infinite recursion

; ~stack-overflow~ !! (
;     blk: [cfor 'i 1 1 1 blk]
;    eval blk
; )

; local variable changeability - this is how it works in R3
(
    test: 'false
    null? cfor 'i 1 3 1 [
        if i = 2 [
            if true? test [break]
            test: 'true
            i: 1
        ]
    ]
)

; local variable type safety
~expect-arg~ !! (
    test: 'false
    cfor 'i 1 2 [
        either true? test [i = 2] [
            test: 'true
            i: 'false
        ]
    ]
)

; CFOR should not bind 'self
[#1529
    (same? 'self cfor 'i 1 1 1 ['self])
]

[#1136
    ~overflow~ !! (
        num: 0
        cfor 'i 9223372036854775806 9223372036854775807 2 [
            num: num + 1
            either num > 1 [break] [okay]
        ]
    )
]

~overflow~ !! (
    num: 0
    cfor 'i -9223372036854775807 -9223372036854775808 -2 [
        num: num + 1
        either num > 1 [break] [okay]
    ]
)

[#1994
    ~overflow~ !! (
        num: 0
        cfor 'i 9223372036854775806 9223372036854775807 9223372036854775807 [
            num: num + 1
            if num <> 1 [break]
        ]
    )
]

~overflow~ !! (
    num: 0
    cfor 'i -9223372036854775807 -9223372036854775808 -9223372036854775808 [
        num: num + 1
        if num <> 1 [break]
    ]
)
