; %loops/while.test.r

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
    success: 'true
    while [null] [success: 'false]
    true? success
)
; Test break and continue
(
    cycling: 'yes
    null? while [cycling = 'yes] [break, cycling: 'no]
)
; Test reactions to break and continue in the condition
(
    was-stopped: 'yes
    while [okay] [
        while [break] []
        was-stopped: 'no
        break
    ]
    yes? was-stopped
)
(
    first-time: 'yes
    was-continued: 'no
    while [okay] [
        if no? first-time [
            was-continued: 'yes
            break
        ]
        first-time: 'no
        while [continue] [break]
        break
    ]
    yes? was-continued
)
(
    success: 'true
    cycling: 'yes
    while [yes? cycling] [cycling: 'no, continue, success: 'false]
    true? success
)
(
    num: 0
    while [okay] [num: 1 break num: 2]
    num = 1
)
; RETURN should stop the loop
(
    cycling: 'yes
    f1: func [return: [integer!]] [
        while [yes? cycling] [
            cycling: 'no
            return 1
        ]
        return 2
    ]
    1 = f1
)
(  ; bug#1519
    cycling: 'yes
    f1: func [return: [integer!]] [
        return while [if yes? cycling [return 1], yes? cycling] [
            cycling: 'no
            2
        ]
    ]
    1 = f1
)

; CONTINUE out of a condition continues any enclosing loop (it does not mean
; continue the LOOP whose condition it appears in)
(
    n: 1
    sum: 0
    while [n < 10] [
        n: n + 1
        if n = 0 [
            while [continue] [
                panic "inner LOOP body should not run"
            ]
            panic "code after inner LOOP should not run"
        ]
        sum: sum + 1
    ]
    sum = 9
)

; THROW should stop the loop
(1 = catch [let cycling: 'yes while [yes? cycling] [throw 1 cycling: 'no]])

[#1519 (
    cycling: 'yes
    1 = catch [while [if yes? cycling [throw 1] <bad>] [cycling: 'no]]
)]

; Test that disarmed errors do not stop the loop and errors can be returned
(
    num: 0
    e: while [num < 10] [num: num + 1 rescue [1 / 0]]
    all [warning? e num = 10]
)
; Recursion check
(
    num1: 0
    num2: ~
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

(
    flag: 'true
    <complete> = catch [
        while [okay] [
            ~()~
            if true? flag [flag: 'false, continue]
            if null [panic]
            throw <complete>
        ]
    ]
)

; CONTINUE and BREAK are definitional
(
    implemented-with-loops: lambda [
        body [block!]
        {sum (0) result}
    ][
        while [result: eval body] [
            sum: sum + result
        ]
        sum
    ]
    counter: 0
    null = while [okay] [
        assert [(1 + 2 + 3) = implemented-with-loops [
            counter: counter + 1
            if counter <= 3 [counter]
        ]]
        implemented-with-loops [
            if counter < 10 [break]
        ]
        panic "Should not reach!"
    ]
)
