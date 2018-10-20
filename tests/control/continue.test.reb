; functions/control/continue.r
; see loop functions for basic continuing functionality
[#1515 ; the "result" of continue should not be assignable
    (a: 1 loop 1 [a: continue] :a == 1)
]
(a: 1 loop 1 [set 'a continue] :a == 1)
(a: 1 loop 1 [set/opt 'a continue] :a == 1)
[#1509 ; the "result" of continue should not be passable to functions
    (a: 1 loop 1 [a: error? continue] :a == 1)
]
[#1535
    (loop 1 [words of continue] true)
]
(loop 1 [values of continue] true)
[#1945
    (loop 1 [spec-of continue] true)
]
; continue should not be caught by try
(a: 1 loop 1 [a: error? trap [continue]] :a == 1)
