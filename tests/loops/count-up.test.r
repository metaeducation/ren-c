; %loops/count-up.test.r

; zero repetition
(
    success: 'true
    count-up 'i 0 [success: 'false]
    true? success
)
(
    success: 'true
    count-up 'i -1 [success: 'false]
    true? success
)
; Test that return stops the loop
(
    f1: func [return: [integer!]] [count-up 'i 1 [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: count-up 'i 2 [num: i rescue [1 / 0]]
    all [warning? e num = 2]
)

; "recursive safety", "locality" and "body constantness" test in one
(count-up 'i 1 b: [not same? 'i b.3])

; recursivity
(
    num: 0
    count-up 'i 5 [
        count-up 'i 2 [num: num + 1]
    ]
    num = 10
)

; local variable type safety

~invalid-type~ !! (
    test: 'false
    count-up 'i 2 [
        either true? test [i = 2] [
            test: 'true
            i: 'false
        ]
    ]
)

(
    success: 'true
    num: 0
    count-up 'i 10 [
        num: num + 1
        success: boolean (true? success) and (i = num)
    ]
    (true? success) and (10 = num)
)
; cycle return value
('false = count-up 'i 1 ['false])
; break cycle
(
    num: 0
    count-up 'i 10 [num: i break]
    num = 1
)
; break return value
(null? count-up 'i 10 [break])
; continue cycle
(
    success: 'true
    count-up 'i 1 [continue, success: 'false]
    true? success
)

; The concept of "opting out" and "opting in" are being tried in COUNT-UP,
; where # means "count effectively forever"...though it can really only
; count up to maxint in the current implementation.
[
    (null = count-up 'i ^void [panic "should not run"])
    (<infinite> = catch [count-up 'i # [if i = 500 [throw <infinite>]]])
]

; mutating the loop variable of a REPEAT affects the loop (Red keeps its own
; internal state, overwritten each body call) https://trello.com/c/V4NKWh5E
(
    sum: 0
    count-up 'i 10 [
        sum: me + 1
        i: 10
    ]
    sum = 1
)
