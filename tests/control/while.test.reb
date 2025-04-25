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
(void? while [null] [])
; zero repetition
(
    success: okay
    while [null] [success: null]
    success
)
; Test break and continue
(
    cycle?: okay
    null? while [cycle?] [break cycle?: null]
)
; Test reactions to break and continue in the condition
(
    was-stopped: okay
    while [okay] [
        while [break] []
        was-stopped: null
        break
    ]
    was-stopped
)
(
    first-time: okay
    was-continued: null
    while [okay] [
        if not first-time [
            was-continued: okay
            break
        ]
        first-time: null
        while [continue] [break]
        break
    ]
    was-continued
)
(
    success: okay
    cycle?: okay
    while [cycle?] [cycle?: null continue success: null]
    success
)
(
    num: 0
    while [okay] [num: 1 break num: 2]
    num = 1
)
; RETURN should stop the loop
(
    cycle?: okay
    f1: func [] [while [cycle?] [cycle?: null return 1] 2]
    1 = f1
)
(  ; bug#1519
    cycle?: okay
    f1: func [] [while [if cycle? [return 1] cycle?] [cycle?: null 2]]
    1 = f1
)
; UNWIND the IF should stop the loop
(
    cycle?: okay
    f1: does [if 1 < 2 [while [cycle?] [cycle?: null unwind :if] 2]]
    null? f1
)

(  ; bug#1519
    cycle?: okay
    f1: does [
        if-not 1 > 2 [
            while [if cycle? [unwind :if-not] cycle?] [cycle?: null 2]
        ]
    ]
    null? f1
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
(1 = catch [cycle?: okay while [cycle?] [throw 1 cycle?: null]])
(  ; bug#1519
    cycle?: okay
    1 = catch [while [if cycle? [throw 1] null] [cycle?: null]]
)
([a 1] = catch/name [cycle?: okay while [cycle?] [throw/name 1 'a cycle?: null]] 'a)
(  ; bug#1519
    cycle?: okay
    [a 1] = catch/name [while [if cycle? [throw/name 1 'a] null] [cycle?: null]] 'a
)
; Test that disarmed errors do not stop the loop and errors can be returned
(
    num: 0
    e: while [num < 10] [num: num + 1 sys/util/rescue [1 / 0]]
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
