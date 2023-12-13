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
    f1: func [return: [any-value?]] [return null]
    null? f1
)
(
    f1: func [return: [error!]] [return trap [1 / 0]]
    error? f1
)

[#1515 ; the "result" of return should not be assignable
    (a: 1 run func [return: [integer!]] [a: return 2] a = 1)
]

(a: 1 reeval reify func [return: [integer!]] [set 'a return 2] a = 1)
(a: 1 run func [return: [integer!]] [set/any 'a return 2] a = 1)

[#1509 ; the "result" of return should not be passable to functions
    (a: 1 reeval reify func [return: [integer!]] [a: error? return 2] a = 1)
]

[#1535
    (run func [return: [blank!]] [words of return blank] true)
]

(reeval reify func [return: [blank!]] [values of return blank] true)

[#1945
    (run func [return: [blank!]] [spec-of return blank] true)
]

; return should not be caught by TRAP
(
    a: 1 reeval reify func [return: [integer!]] [a: error? trap [return 2]]
    a = 1
)

(
    success: true
    f1: func [return: [~]] [return ~, success: false]
    f1
    success
)
(
    f1: func [return: [~]] [return ~]
    trash' = ^ f1
)
[#1515 (  ; the "result" of a return should not be assignable
    a: 1
    run func [return: [~]] [a: return ~]
    a = 1
)]
(a: 1 reeval reify func [return: [~]] [set 'a return ~] :a =? 1)
(a: 1 reeval reify func [return: [~]] [set/any 'a return ~] :a =? 1)
[#1509 (  ; the "result" of a return should not be passable to functions
    a: 1
    run func [return: [~]] [a: error? return ~]
    a = 1
)]
[#1535
    (reeval reify func [return: [~]] [words of return ~] true)
]
(run func [return: [~]] [values of return ~] true)
[#1945
    (run func [return: [~]] [spec-of return ~] true)
]
