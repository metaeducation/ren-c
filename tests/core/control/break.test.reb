; functions/control/break.r
; see loop functions for basic breaking functionality
; just testing return values, but written as if break could fail altogether
; in case that becomes an issue. break failure tests are with the functions
; that they are failing to break from.
[void? loop 1 [break 2]]
; break/return should return argument
[blank? loop 1 [break/return blank 2]]
[false =? loop 1 [break/return false 2]]
[true =? loop 1 [break/return true 2]]
[void? loop 1 [break/return () 2]]
[error? loop 1 [break/return try [1 / 0] 2]]
; the "result" of break should not be assignable, bug#1515
[a: 1 loop 1 [a: break] :a =? 1]
[a: 1 loop 1 [set 'a break] :a =? 1]
[a: 1 loop 1 [set/opt 'a break] :a =? 1]
[a: 1 loop 1 [a: break/return 2] :a =? 1]
[a: 1 loop 1 [set 'a break/return 2] :a =? 1]
[a: 1 loop 1 [set/opt 'a break/return 2] :a =? 1]
; the "result" of break should not be passable to functions, bug#1509
[a: 1 loop 1 [a: error? break] :a =? 1] ; error? function takes 1 arg
[a: 1 loop 1 [a: error? break/return 2] :a =? 1]
[a: 1 loop 1 [a: type-of break] :a =? 1] ; type-of function takes 1-2 args
[foo: func [x y] [9] a: 1 loop 1 [a: foo break 5] :a =? 1] ; foo takes 2 args
[foo: func [x y] [9] a: 1 loop 1 [a: foo 5 break] :a =? 1]
[foo: func [x y] [9] a: 1 loop 1 [a: foo break break] :a =? 1]
; check that BREAK is evaluated (and not CONTINUE):
[foo: func [x y] [] a: 1 loop 2 [a: a + 1 foo break continue a: a + 10] :a =? 2]
; check that BREAK is not evaluated (but CONTINUE is):
[foo: func [x y] [] a: 1 loop 2 [a: a + 1 foo continue break a: a + 10] :a =? 3]
; bug#1535
[loop 1 [words-of break] true]
[loop 1 [values-of break] true]
; bug#1945
[loop 1 [spec-of break] true]
; the "result" of break should not be caught by try
[a: 1 loop 1 [a: error? try [break]] :a =? 1]
