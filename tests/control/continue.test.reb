; functions/control/continue.r
; see loop functions for basic continuing functionality
; the "result" of continue should not be assignable, bug#1515
[a: 1 loop 1 [a: continue] :a =? 1]
[a: 1 loop 1 [set 'a continue] :a =? 1]
[a: 1 loop 1 [set/only 'a continue] :a =? 1]
; the "result" of continue should not be passable to functions, bug#1509
[a: 1 loop 1 [a: error? continue] :a =? 1]
; bug#1535
[loop 1 [words-of continue] true]
[loop 1 [values-of continue] true]
; bug#1945
[loop 1 [spec-of continue] true]
; continue should not be caught by try
[a: 1 loop 1 [a: error? try [continue]] :a =? 1]
