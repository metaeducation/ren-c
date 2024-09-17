; functions/series/pick.r
<64bit>
(error? sys/util/rescue [pick at [1 2 3 4 5] 3 -9223372036854775808])
(null? pick at [1 2 3 4 5] 3 -2147483648)
(null? pick at [1 2 3 4 5] 3 -2147483647)
(null? pick at [1 2 3 4 5] 3 -3)
(1 = pick at [1 2 3 4 5] 3 -2)
(2 = pick at [1 2 3 4 5] 3 -1)
(null = pick at [1 2 3 4 5] 3 0)
(3 = pick at [1 2 3 4 5] 3 1)
(4 = pick at [1 2 3 4 5] 3 2)
(5 = pick at [1 2 3 4 5] 3 3)
(null? pick at [1 2 3 4 5] 3 4)
(null? pick at [1 2 3 4 5] 3 2147483647)
<64bit>
(error? sys/util/rescue [pick at [1 2 3 4 5] 3 9223372036854775807])
; string
<64bit>
(error? sys/util/rescue [pick at "12345" 3 -9223372036854775808])
(null? pick at "12345" 3 -2147483648)
(null? pick at "12345" 3 -2147483647)
(null? pick at "12345" 3 -3)
(#"1" = pick at "12345" 3 -2)
(#"2" = pick at "12345" 3 -1)
[#857
    (null = pick at "12345" 3 0)
]
(#"3" = pick at "12345" 3 1)
(#"4" = pick at "12345" 3 2)
(#"5" = pick at "12345" 3 3)
(null? pick at "12345" 3 4)
(null? pick at "12345" 3 2147483647)
<64bit>
(error? sys/util/rescue [pick at "12345" 3 9223372036854775807])
