; %loops/break.test.r
;
; see loop functions for basic breaking functionality
; just testing return values, but written as if break could fail altogether
; in case that becomes an issue. break failure tests are with the functions
; that they are failing to break from.

(null? repeat 1 [break 2])

; the "result" of break should not be assignable
[#1515
    (a: 1, repeat 1 [a: break], :a = 1)
]
[#1515
    (a: 1, repeat 1 [set $a break], :a = 1)
]
[#1515
    (a: 1, repeat 1 [set:any $a break], :a = 1)
]

; the "result" of break should not be passable to functions
[#1509
    (a: 1, repeat 1 [a: warning? break], :a = 1)
]
[#1509
    (a: 1, repeat 1 [a: type of break], :a = 1)
]
[#1509
    (foo: func [x y] [9], a: 1, repeat 1 [a: foo break 5], :a = 1)
]
[#1509
    (foo: func [x y] [9], a: 1, repeat 1 [a: foo 5 break], :a = 1)
]
[#1509
    (foo: func [x y] [9], a: 1, repeat 1 [a: foo break break], :a = 1)
]

; check that BREAK is evaluated (and not CONTINUE):
(
    foo: func [x y] [] a: 1 repeat 2 [
        a: a + 1 foo (break) continue a: a + 10
    ]
    a = 2
)

; check that BREAK is not evaluated (but CONTINUE is):
; Note: CONTINUE is variadic and will take parameters if available, so there
; has to be an expression barrier or otherwise.
(
    foo: func [x y] [] a: 1 repeat 2 [
        a: a + 1 foo (continue) break a: a + 10
    ]
    a = 3
)

[#1535 #1535
    (repeat 1 [words of break] ok)
]
[#1535
    (repeat 1 [values of break] ok)
]

[#1945
    (repeat 1 [spec-of break] ok)
]

; the "result" of break should not be caught by try
(a: 1 repeat 1 [a: warning? rescue [break]] a = 1)
