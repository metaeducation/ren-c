; functions/control/until.r
(
    num: 0
    until [num: num + 1 num > 9]
    num = 10
)
; Test body-block return values
(1 = until [1])
; Test break
(null? until [break true])
; Test continue
(
    success: true
    cycle?: true
    until [if cycle? [cycle?: false continue success: false] true]
    success
)
; Test that return stops the loop
(
    f1: func [] [until [return 1]]
    1 = f1
)
; Test that errors do not stop the loop
(1 = until [trap [1 / 0] 1])
; Recursion check
(
    num1: 0
    num3: 0
    until [
        num2: 0
        until [
            num3: num3 + 1
            1 < (num2: num2 + 1)
        ]
        4 < (num1: num1 + 1)
    ]
    10 = num3
)
