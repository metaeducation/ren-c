; %loop-test.reb
;
; LOOP in Ren-C acts as an arity-2 looping operation, like WHILE from Rebol2
; and R3-Alpha.
;
; The reason for the name change is to be consistent with PARSE's use of WHILE
; as arity-1.  That also naturally makes WHILE and UNTIL pair together as
; taking a single argument.

(
    num: 0
    loop [num < 10] [num: num + 1]
    num = 10
)
; Test body-block return values
[#37 (
    num: 0
    1 = loop [num < 1] [num: num + 1]
)]
('~null~ = ^ loop [false] [])
; zero repetition
(
    success: true
    loop [false] [success: false]
    success
)
; Test break and continue
(
    cycle?: true
    null? loop [cycle?] [break cycle?: false]
)
; Test reactions to break and continue in the condition
(
    was-stopped: true
    loop [true] [
        loop [break] []
        was-stopped: false
        break
    ]
    was-stopped
)
(
    first-time: true
    was-continued: false
    loop [true] [
        if not first-time [
            was-continued: true
            break
        ]
        first-time: false
        loop [continue] [break]
        break
    ]
    was-continued
)
(
    success: true
    cycle?: true
    loop [cycle?] [cycle?: false, continue, success: false]
    success
)
(
    num: 0
    loop [true] [num: 1 break num: 2]
    num = 1
)
; RETURN should stop the loop
(
    cycle?: true
    f1: func [] [loop [cycle?] [cycle?: false return 1] 2]
    1 = f1
)
(  ; bug#1519
    cycle?: true
    f1: func [] [loop [if cycle? [return 1] cycle?] [cycle?: false 2]]
    1 = f1
)

; CONTINUE out of a condition continues any enclosing loop (it does not mean
; continue the LOOP whose condition it appears in)
(
    n: 1
    sum: 0
    loop [n < 10] [
        n: n + 1
        if n = 0 [
            loop [continue] [
                fail "inner LOOP body should not run"
            ]
            fail "code after inner LOOP should not run"
        ]
        sum: sum + 1
    ]
    sum = 9
)

; THROW should stop the loop
(1 = catch [cycle?: true loop [cycle?] [throw 1 cycle?: false]])
(  ; bug#1519
    cycle?: true
    1 = catch [loop [if cycle? [throw 1] false] [cycle?: false]]
)
([a 1] = catch/name [cycle?: true loop [cycle?] [throw/name 1 'a cycle?: false]] 'a)
(  ; bug#1519
    cycle?: true
    [a 1] = catch/name [loop [if cycle? [throw/name 1 'a] false] [cycle?: false]] 'a
)
; Test that disarmed errors do not stop the loop and errors can be returned
(
    num: 0
    e: loop [num < 10] [num: num + 1 trap [1 / 0]]
    all [error? e num = 10]
)
; Recursion check
(
    num1: 0
    num3: 0
    loop [num1 < 5] [
        num2: 0
        loop [num2 < 2] [
            num3: num3 + 1
            num2: num2 + 1
        ]
        num1: num1 + 1
    ]
    10 = num3
)
