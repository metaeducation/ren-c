; %loops/continue.test.r
;
; see loop functions for basic continuing functionality

[#1515 ; the "result" of continue should not be assignable
    (a: 1 repeat 1 [a: continue] a = 1)
]
(a: 1 repeat 1 [set $a continue] a = 1)
(a: 1 repeat 1 [set:any $a continue] a = 1)
[#1509 ; the "result" of continue should not be passable to functions
    (a: 1 repeat 1 [a: warning? continue] a = 1)
]
[#1535
    (repeat 1 [words of continue] ok)
]
(repeat 1 [values of continue] ok)
; continue should not be caught by try
(a: 1 repeat 1 [a: warning? rescue [continue]] a = 1)

; CONTINUE with a value pretends loop body finished with that result.

(<result> = repeat 1 [continue:with <result> <not-result>])
(
    [2 <big> <big>] = map-each 'x [1 2000 3000] [
        if x > 1000 [continue:with <big>]
        x + 1
    ]
)
