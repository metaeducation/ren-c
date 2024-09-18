; functions/control/return.r
(
    f1: func [] [return 1 2]
    1 = f1
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
    f1: func [] [return sys/util/rescue [1 / 0]]
    error? f1
)
[#1515 ; the "result" of return should not be assignable
    (a: 1 reeval func [] [a: return 2] :a =? 1)
]
(a: 1 reeval func [] [set 'a return 2] :a =? 1)
[#1509 ; the "result" of return should not be passable to functions
    (a: 1 reeval func [] [a: error? return 2] :a =? 1)
]
[#1535
    (reeval func [] [words of return blank] true)
]
(reeval func [] [values of return blank] true)
[#1945
    (reeval func [] [spec-of return blank] true)
]
; return should not be caught by try
(a: 1 reeval func [] [a: error? sys/util/rescue [return 2]] :a =? 1)
