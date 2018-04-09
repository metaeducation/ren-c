; functions/control/leave.r
(
    success: true
    f1: proc [] [leave success: false]
    f1
    success
)
(
    f1: proc [] [leave]
    void? f1
)
[#1515 ; the "result" of leave should not be assignable
    (a: 1 eval proc [] [a: leave] :a =? 1)
]
(a: 1 eval proc [] [set 'a leave] :a =? 1)
(a: 1 eval proc [] [set/only 'a leave] :a =? 1)
[#1509 ; the "result" of exit should not be passable to functions
    (a: 1 eval proc [] [a: error? leave] :a =? 1)
]
[#1535
    (eval proc [] [words of leave] true)
]
(eval proc [] [values of leave] true)
[#1945
    (eval proc [] [spec-of leave] true)
]
