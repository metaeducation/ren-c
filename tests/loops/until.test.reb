; %loops/until.test.reb

(
    num: 0
    until [num: num + 1 num > 9]
    num = 10
)
; Test body-block return values
(1 = until [1])
; Test break
(null? until [break 'true])
; Test continue
(
    success: 'true
    cycling: 'yes
    until [if yes? cycling [cycling: 'no, continue, success: 'false] okay]
    true? success
)
; Test that return stops the loop
(
    f1: func [return: [integer!]] [until [return 1]]
    1 = f1
)
; Test that errors do not stop the loop
(1 = until [trap [1 / 0] 1])
; Recursion check
(
    num1: 0
    num2: ~
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


; === PREDICATES ===

(
    x: [2 4 6 8 7 9 11 30]
    all [
        7 = until/predicate [take x] cascade [even?/ not/]
        x = [9 11 30]
    ]
)(
    x: [1 "hi" <foo> _ <bar> "baz" 2]
    all [
        blank? until/predicate [take x] z -> [blank? z]
        x = [<bar> "baz" 2]
    ]
)

; UNTIL truth tests the results, which means unstable isotopes have to be
; decayed to run that test.
[
    (1 = until [pack [1 2]])
    ('~['1 '2]~ = until [meta pack [1 2]])
]

[
    (
        'false = until [match [boolean?] 'false]
    )
]

(
    n: 1
    9 = until/predicate [n: n + 2] lambda [x] [x > 7]
)
