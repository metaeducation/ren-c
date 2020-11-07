; functions/control/while.r
(
    num: 0
    while [num < 10] [num: num + 1]
    num = 10
)
; Test body-block return values
[#37 (
    num: 0
    1 = while [num < 1] [num: num + 1]
)]
(blank? while [false] [])
; zero repetition
(
    success: true
    while [false] [success: false]
    success
)
; Test break and continue
(
    cycle?: true
    null? while [cycle?] [break cycle?: false]
)
; Test reactions to break and continue in the condition
(
    was-stopped: true
    while [true] [
        while [break] []
        was-stopped: false
        break
    ]
    was-stopped
)
(
    first-time: true
    was-continued: false
    while [true] [
        if not first-time [
            was-continued: true
            break
        ]
        first-time: false
        while [continue] [break]
        break
    ]
    was-continued
)
(
    success: true
    cycle?: true
    while [cycle?] [cycle?: false, continue, success: false]
    success
)
(
    num: 0
    while [true] [num: 1 break num: 2]
    num = 1
)
; RETURN should stop the loop
(
    cycle?: true
    f1: func [] [while [cycle?] [cycle?: false return 1] 2]
    1 = f1
)
(  ; bug#1519
    cycle?: true
    f1: func [] [while [if cycle? [return 1] cycle?] [cycle?: false 2]]
    1 = f1
)

; CONTINUE out of a condition continues any enclosing loop (it does not mean
; continue the WHILE whose condition it appears in)
(
    n: 1
    sum: 0
    while [n < 10] [
        n: n + 1
        if n = 0 [
            while [continue] [
                fail "inner WHILE body should not run"
            ]
            fail "code after inner WHILE should not run"
        ]
        sum: sum + 1
    ]
    sum = 9
)

; THROW should stop the loop
(1 = catch [cycle?: true while [cycle?] [throw 1 cycle?: false]])
(  ; bug#1519
    cycle?: true
    1 = catch [while [if cycle? [throw 1] false] [cycle?: false]]
)
([a 1] = catch/name [cycle?: true while [cycle?] [throw/name 1 'a cycle?: false]] 'a)
(  ; bug#1519
    cycle?: true
    [a 1] = catch/name [while [if cycle? [throw/name 1 'a] false] [cycle?: false]] 'a
)
; Test that disarmed errors do not stop the loop and errors can be returned
(
    num: 0
    e: while [num < 10] [num: num + 1 trap [1 / 0]]
    all [error? e num = 10]
)
; Recursion check
(
    num1: 0
    num3: 0
    while [num1 < 5] [
        num2: 0
        while [num2 < 2] [
            num3: num3 + 1
            num2: num2 + 1
        ]
        num1: num1 + 1
    ]
    10 = num3
)
