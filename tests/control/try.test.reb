; functions/control/try.r
(
    e: try [1 / 0]
    e/id = 'zero-divide
)
(
    success: true
    error? try [
        1 / 0
        success: false
    ]
    success
)
(
    success: true
    f1: does [
        1 / 0
        success: false
    ]
    error? try [f1]
    success
)
; testing TRY/EXCEPT
[#822
    (error? try/except [make error! ""] [0])
]
(try/except [fail make error! ""] [true])
(try/except [1 / 0] :error?)
(try/except [1 / 0] func [e] [error? e])
(try/except [true] func [e] [false])
[#1514
    (error? try [try/except [1 / 0] :add])
]

[#1506 ((
    10 = eval does [try [return 10] 20]
))]
