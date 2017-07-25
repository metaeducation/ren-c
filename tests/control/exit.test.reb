; functions/control/exit.r
[
    success: true
    f1: does [exit success: false]
    f1
    success
]
[
    f1: does [exit]
    void? f1
]
; the "result" of exit should not be assignable, bug#1515
[a: 1 eval does [a: exit] :a =? 1]
[a: 1 eval does [set 'a exit] :a =? 1]
[a: 1 eval does [set/only 'a exit] :a =? 1]
; the "result" of exit should not be passable to functions, bug#1509
[a: 1 eval does [a: error? exit] :a =? 1]
; bug#1535
[eval does [words-of exit] true]
[eval does [values-of exit] true]
; bug#1945
[eval does [spec-of exit] true]
