; functions/control/return.r
(
    f1: func [] [return 1 2]
    1 == f1
)
(
    success: true
    f1: func [] [return 1 success: false]
    f1
    success
)
; return value tests
(
    f1: func [] [return null]
    null? f1
)
(
    f1: func [] [return trap [1 / 0]]
    error? f1
)
[#1515 ; the "result" of return should not be assignable
    (a: 1 eval func [] [a: return 2] :a == 1)
]
(a: 1 eval func [] [set 'a return 2] :a == 1)
(a: 1 eval func [] [set/opt 'a return 2] :a == 1)
[#1509 ; the "result" of return should not be passable to functions
    (a: 1 eval func [] [a: error? return 2] :a == 1)
]
[#1535
    (eval func [] [words of return blank] true)
]
(eval func [] [values of return blank] true)
[#1945
    (eval func [] [spec-of return blank] true)
]
; return should not be caught by try
(a: 1 eval func [] [a: error? trap [return 2]] :a == 1)
