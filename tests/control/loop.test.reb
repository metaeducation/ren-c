; functions/control/loop.r
(
    num: 0
    loop 10 [num: num + 1]
    10 = num
)
; cycle return value
(bar? loop 1 [false])
; break cycle
(
    num: 0
    loop 10 [num: num + 1 break]
    num = 1
)
; break return value
(blank? loop 10 [break])
; continue cycle
(
    success: true
    loop 1 [continue success: false]
    success
)
; zero repetition
(
    success: true
    loop 0 [success: false]
    success
)
(
    success: true
    loop -1 [success: false]
    success
)
; Test that return stops the loop
(
    f1: does [loop 1 [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: loop 2 [num: num + 1 try [1 / 0]]
    all [error? e num = 2]
)
; loop recursivity
(
    num: 0
    loop 5 [
        loop 2 [num: num + 1]
    ]
    num = 10
)
; recursive use of 'break
(
    f: func [x] [
        loop 1 [
            either x = 1 [
                use [break] [
                    break: 1
                    f 2
                    1 = get/only 'break
                ]
            ][
                false
            ]
        ]
    ]
    f 1
)

; mutating the loop variable of a REPEAT affects the loop (Red keeps its own
; internal state, overwritten each body call) https://trello.com/c/V4NKWh5E
(
    sum: 0
    repeat i 10 [
        sum: me + 1
        i: 10
    ]
    sum = 1
)
