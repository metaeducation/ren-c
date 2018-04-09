; functions/control/until.r
(
    num: 0
    loop-until [num: num + 1 num > 9]
    num = 10
)
; Test body-block return values
(1 = loop-until [1])
; Test break
(blank? loop-until [break true])
; Test continue
(
    success: true
    cycle?: true
    loop-until [if cycle? [cycle?: false continue success: false] true]
    success
)
; Test that return stops the loop
(
    f1: does [loop-until [return 1]]
    1 = f1
)
; Test that errors do not stop the loop
(1 = loop-until [try [1 / 0] 1])
; Recursion check
(
    num1: 0
    num3: 0
    loop-until [
        num2: 0
        loop-until [
            num3: num3 + 1
            1 < (num2: num2 + 1)
        ]
        4 < (num1: num1 + 1)
    ]
    10 = num3
)
