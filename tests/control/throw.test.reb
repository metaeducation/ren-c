; functions/control/throw.r
; see functions/control/catch.r for basic functionality
[#1515 ; the "result" of throw should not be assignable
    (a: 1 catch [a: throw 2] :a =? 1)
]
(a: 1 catch [set $a throw 2] :a =? 1)
(a: 1 catch [set/any $a throw 2] :a =? 1)
(a: 1 catch/name [a: throw/name 2 'b] 'b :a =? 1)
(a: 1 catch/name [set $a throw/name 2 'b] 'b :a =? 1)
(a: 1 catch/name [set/any $a throw/name 2 'b] 'b :a =? 1)
[#1509 ; the "result" of throw should not be passable to functions
    (a: 1 catch [a: error? throw 2] :a =? 1)
]
[#1535
    (blank = catch [word of throw blank])
]
(blank = catch [values of throw blank])
[#1945
    (blank = catch [spec-of throw blank])
]
(a: 1 catch/name [a: error? throw/name 2 'b] 'b :a =? 1)
; throw should not be caught by try
(a: 1 catch [a: error? trap [throw 2]] :a =? 1)
(a: 1 catch/name [a: error? trap [throw/name 2 'b]] 'b :a =? 1)
