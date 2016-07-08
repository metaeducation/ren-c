; functions/control/return.r
[
    f1: does [return 1 2]
    1 = f1
]
[
    success: true
    f1: does [return 1 success: false]
    f1
    success
]
; return value tests
[
    f1: does [return ()]
    void? f1
]
[
    f1: does [return try [1 / 0]]
    error? f1
]
; the "result" of return should not be assignable, bug#1515
[a: 1 eval does [a: return 2] :a =? 1]
[a: 1 eval does [set 'a return 2] :a =? 1]
[a: 1 eval does [set/opt 'a return 2] :a =? 1]
; the "result" of return should not be passable to functions, bug#1509
[a: 1 eval does [a: error? return 2] :a =? 1]
; bug#1535
[eval does [words-of return blank] true]
[eval does [values-of return blank] true]
; bug#1945
[eval does [spec-of return blank] true]
; return should not be caught by try
[a: 1 eval does [a: error? try [return 2]] :a =? 1]
