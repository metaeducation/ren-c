; functions/control/while.r
[
    num: 0
    while [num < 10] [num: num + 1]
    num = 10
]
; bug#37
; Test body-block return values
[
    num: 0
    1 = while [num < 1] [num: num + 1]
]
[void? while [false] []]
; zero repetition
[
    success: true
    while [false] [success: false]
    success
]
; Test break, break/return and continue
[cycle?: true void? while [cycle?] [break cycle?: false]]
; Test reactions to break and continue in the condition
[
    was-stopped: true
    while [true] [
        while [break] []
        was-stopped: false
        break
    ]
    was-stopped
]
[
    first-time: true
    was-continued: false
    while [true] [
        unless first-time [
            was-continued: true
            break
        ]
        first-time: false
        while [continue] [break]
        break
    ]
    was-continued
]
[
    success: true
    cycle?: true
    while [cycle?] [cycle?: false continue success: false]
    success
]
[
    num: 0
    while [true] [num: 1 break num: 2]
    num = 1
]
; RETURN should stop the loop
[
    cycle?: true
    f1: does [while [cycle?] [cycle?: false return 1] 2]
    1 = f1
]
[  ; bug#1519
    cycle?: true
    f1: does [while [if cycle? [return 1] cycle?] [cycle?: false 2]]
    1 = f1
]
; EXIT/FROM the IF should stop the loop
[
    cycle?: true
    f1: does [if 1 < 2 [while [cycle?] [cycle?: false exit/from :if] 2]]
    void? f1
]
[  ; bug#1519
    cycle?: true
    f1: does [
        unless 1 > 2 [
            while [if cycle? [exit/from :unless] cycle?] [cycle?: false 2]
        ]
    ]
    void? f1
]
; THROW should stop the loop
[1 = catch [cycle?: true while [cycle?] [throw 1 cycle?: false]]]
[  ; bug#1519
    cycle?: true
    1 = catch [while [if cycle? [throw 1] false] [cycle?: false]]
]
[1 = catch/name [cycle?: true while [cycle?] [throw/name 1 'a cycle?: false]] 'a]
[  ; bug#1519
    cycle?: true
    1 = catch/name [while [if cycle? [throw/name 1 'a] false] [cycle?: false]] 'a
]
; Test that disarmed errors do not stop the loop and errors can be returned
[
    num: 0
    e: while [num < 10] [num: num + 1 try [1 / 0]]
    all [error? e num = 10]
]
; Recursion check
[
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
]
