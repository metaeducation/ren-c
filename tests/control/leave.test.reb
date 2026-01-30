; functions/control/leave.r
(
    success: okay
    f1: func [return: [trash!]] [(return) success: null]
    f1
    success
)
(
    f1: func [return: [trash!]] [return]
    trash? f1
)
[#1515 ; the "result" of an arity-0 return should not be assignable
    (a: 1 reeval func [return: [trash!]] [a: return] a = 1)
]
(a: 1 reeval func [return: [trash!]] [set 'a return] a = 1)
[#1509 ; the "result" of an arity-0 return should not be passable to functions
    (a: 1 reeval func [return: [trash!]] [a: error? return] a = 1)
]
[#1535
    (reeval func [return: [trash!]] [words of return] okay)
]
(reeval func [return: [trash!]] [values of return] okay)
[#1945
    (reeval func [return: [trash!]] [spec-of return] okay)
]
