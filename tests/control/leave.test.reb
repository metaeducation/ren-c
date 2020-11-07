; functions/control/leave.r
(
    success: true
    f1: func [return: <void>] [return, success: false]
    f1
    success
)
(
    f1: func [return: <void>] [return]
    void? f1
)
[#1515 ; the "result" of an arity-0 return should not be assignable
    (a: 1 reeval func [return: <void>] [a: return] :a =? 1)
]
(a: 1 reeval func [return: <void>] [set 'a return] :a =? 1)
(a: 1 reeval func [return: <void>] [set/opt 'a return] :a =? 1)
[#1509 ; the "result" of an arity-0 return should not be passable to functions
    (a: 1 reeval func [return: <void>] [a: error? return] :a =? 1)
]
[#1535
    (reeval func [return: <void>] [words of return] true)
]
(reeval func [return: <void>] [values of return] true)
[#1945
    (reeval func [return: <void>] [spec-of return] true)
]
