; functions/control/leave.r
[
    success: true
    f1: proc [] [leave success: false]
    f1
    success
]
[
    f1: proc [] [leave]
    void? f1
]
; the "result" of leave should not be assignable, bug#1515
[a: 1 eval proc [] [a: leave] :a =? 1]
[a: 1 eval proc [] [set 'a leave] :a =? 1]
[a: 1 eval proc [] [set/only 'a leave] :a =? 1]
; the "result" of exit should not be passable to functions, bug#1509
[a: 1 eval proc [] [a: error? leave] :a =? 1]
; bug#1535
[eval proc [] [words of leave] true]
[eval proc [] [values of leave] true]
; bug#1945
[eval proc [] [spec-of leave] true]
