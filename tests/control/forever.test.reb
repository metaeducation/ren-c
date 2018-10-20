; functions/control/forever.r
(
    num: 0
    forever [
        num: num + 1
        if num == 10 [break]
    ]
    num == 10
)
; Test break and continue
(null? forever [break])
(
    success: true
    cycle?: true
    forever [if cycle? [cycle?: false continue success: false] break]
    success
)
; Test that arity-1 return stops the loop
(
    f1: func [] [forever [return 1]]
    1 == f1
)
; Test that arity-0 return stops the loop
(void? eval func [return: <void>] [forever [return]])
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: _
    forever [
        num: num + 1
        if num == 10 [e: trap [1 / 0] break]
        trap [1 / 0]
    ]
    error? e and [num == 10]
)
; Recursion check
(
    num1: 0
    num3: 0
    forever [
        if num1 == 5 [break]
        num2: 0
        forever [
            if num2 == 2 [break]
            num3: num3 + 1
            num2: num2 + 1
        ]
        num1: num1 + 1
    ]
    10 == num3
)
