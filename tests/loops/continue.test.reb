; %loops/continue.test.reb
;
; see loop functions for basic continuing functionality

[#1515 ; the "result" of continue should not be assignable
    (a: 1 repeat 1 [a: continue] :a =? 1)
]
(a: 1 repeat 1 [set 'a continue] :a =? 1)
(a: 1 repeat 1 [set/opt 'a continue] :a =? 1)
[#1509 ; the "result" of continue should not be passable to functions
    (a: 1 repeat 1 [a: error? continue] :a =? 1)
]
[#1535
    (repeat 1 [words of continue] true)
]
(repeat 1 [values of continue] true)
[#1945
    (repeat 1 [spec-of continue] true)
]
; continue should not be caught by try
(a: 1 repeat 1 [a: error? trap [continue]] :a =? 1)

; CONTINUE with a value pretends loop body finished with that result.

(<result> = repeat 1 [continue <result> <not-result>])
(
    [2 <big> <big>] = map-each x [1 2000 3000] [
        if x > 1000 [continue <big>]
        x + 1
    ]
)
