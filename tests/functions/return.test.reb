; functions/control/return.r

(
    f1: func [return: [integer!]] [return 1 2]
    1 = f1
)
(
    success: true
    f1: func [return: [integer!]] [return 1 success: false]
    f1
    success
)

; return value tests
(
    f1: func [return: [<opt> any-value!]] [return null]
    null? f1
)
(
    f1: func [return: [error!]] [return trap [1 / 0]]
    error? f1
)

[#1515 ; the "result" of return should not be assignable
    (a: 1 reeval func [return: [integer!]] [a: return 2] a = 1)
]

(a: 1 reeval func [return: [integer!]] [set 'a return 2] a = 1)
(a: 1 reeval func [return: [integer!]] [set/opt 'a return 2] a = 1)

[#1509 ; the "result" of return should not be passable to functions
    (a: 1 reeval func [return: [integer!]] [a: error? return 2] a = 1)
]

[#1535
    (reeval func [return: [blank!]] [words of return blank] true)
]

(reeval func [return: [blank!]] [values of return blank] true)

[#1945
    (reeval func [return: [blank!]] [spec-of return blank] true)
]

; return should not be caught by TRAP
(
    a: 1 reeval func [return: [integer!]] [a: error? trap [return 2]]
    a = 1
)

(
    success: true
    f1: func [return: <none>] [return none, success: false]
    f1
    success
)
(
    f1: func [return: <none>] [return none]
    '~ = ^ f1
)
[#1515 ; the "result" of a none return should not be assignable
    (a: 1 reeval func [return: <none>] [a: return none] :a =? 1)
]
(a: 1 reeval func [return: <none>] [set 'a return none] :a =? 1)
(a: 1 reeval func [return: <none>] [set/opt 'a return none] :a =? 1)
[#1509 ; the "result" of a none return should not be passable to functions
    (a: 1 reeval func [return: <none>] [a: error? return none] :a =? 1)
]
[#1535
    (reeval func [return: <none>] [words of return none] true)
]
(reeval func [return: <none>] [values of return none] true)
[#1945
    (reeval func [return: <none>] [spec-of return none] true)
]
