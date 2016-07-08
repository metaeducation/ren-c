; functions/control/forever.r
[
    num: 0
    forever [
        num: num + 1
        if num = 10 [break]
    ]
    num = 10
]
; Test break, break/return and continue
[void? forever [break]]
[1 = forever [break/return 1]]
[
    success: true
    cycle?: true
    forever [if cycle? [cycle?: false continue success: false] break]
    success
]
; Test that return stops the loop
[
    f1: does [forever [return 1]]
    1 = f1
]
; Test that exit stops the loop
[void? eval does [forever [exit]]]
; Test that errors do not stop the loop and errors can be returned
[
    num: 0
    e: forever [
        num: num + 1
        if num = 10 [break/return try [1 / 0]]
        try [1 / 0]
    ]
    all [error? e num = 10]
]
; Recursion check
[
    num1: 0
    num3: 0
    forever [
        if num1 = 5 [break]
        num2: 0
        forever [
            if num2 = 2 [break]
            num3: num3 + 1
            num2: num2 + 1
        ]
        num1: num1 + 1
    ]
    10 = num3
]
