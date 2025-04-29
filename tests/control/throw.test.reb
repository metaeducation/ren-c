; functions/control/throw.r
; see functions/control/catch.r for basic functionality
[#1515 ; the "result" of throw should not be assignable
    (a: 1 catch [a: throw 2] a = 1)
]
(a: 1 catch [set 'a throw 2] a = 1)
(a: 1 catch/name [a: throw/name 2 'b] 'b a = 1)
(a: 1 catch/name [set 'a throw/name 2 'b] 'b a = 1)
[#1509 ; the "result" of throw should not be passable to functions
    (a: 1 catch [a: error? throw 2] a = 1)
]
[#1535
    (catch [word of throw blank] okay)
]
(catch [values of throw blank] okay)
[#1945
    (catch [spec-of throw blank] okay)
]
(a: 1 catch/name [a: error? throw/name 2 'b] 'b a = 1)
; throw should not be caught by try
(a: 1 catch [a: error? sys/util/rescue [throw 2]] a = 1)
(a: 1 catch/name [a: error? sys/util/rescue [throw/name 2 'b]] 'b a = 1)
