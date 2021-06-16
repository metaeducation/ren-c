; functions/control/loop.r
(
    num: 0
    repeat 10 [num: num + 1]
    10 = num
)
; cycle return value
(false = repeat 1 [false])
; break cycle
(
    num: 0
    repeat 10 [num: num + 1 break]
    num = 1
)
; break return value
(null? repeat 10 [break])
; continue cycle
(
    success: true
    repeat 1 [continue, success: false]
    success
)
; zero repetition
(
    success: true
    repeat 0 [success: false]
    success
)
(
    success: true
    repeat -1 [success: false]
    success
)
; Test that return stops the loop
(
    f1: func [] [repeat 1 [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: repeat 2 [num: num + 1 trap [1 / 0]]
    all [error? e num = 2]
)
; loop recursivity
(
    num: 0
    repeat 5 [
        repeat 2 [num: num + 1]
    ]
    num = 10
)
; recursive use of 'break
(
    f: func [x] [
        repeat 1 [
            either x = 1 [
                use [break] [
                    break: 1
                    f 2
                    1 = get 'break
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
    count-up i 10 [
        sum: me + 1
        i: 10
    ]
    sum = 1
)

; test that a continue which interrupts code using the mold buffer does not
; leave the gathered material in the mold buffer
;
(
    (the ') = ^ repeat 2 [unspaced ["abc" continue]]
)

; Test ACTION! as branch
;
[
    (did branch: does [if nbreak = n [break] n: n + 1])

    (nbreak: ('...), n: 0, (the ') = ^ repeat 0 :branch)
    (nbreak: ('...), n: 0, 3 = repeat 3 :branch)
    (nbreak: 2, n: 0, null? repeat 3 :branch)
]
