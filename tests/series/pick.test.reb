; functions/series/pick.r
<64bit>
(error? try [pick at [1 2 3 4 5] 3 -9223372036854775808])
(void? pick* at [1 2 3 4 5] 3 -2147483648)
(void? pick* at [1 2 3 4 5] 3 -2147483647)
(void? pick* at [1 2 3 4 5] 3 -3)
(void? pick* at [1 2 3 4 5] 3 -2)
(1 = pick at [1 2 3 4 5] 3 -1)
(2 = pick at [1 2 3 4 5] 3 0)
(3 = pick at [1 2 3 4 5] 3 1)
(4 = pick at [1 2 3 4 5] 3 2)
(5 = pick at [1 2 3 4 5] 3 3)
(void? pick* at [1 2 3 4 5] 3 4)
(void? pick* at [1 2 3 4 5] 3 2147483647)
<64bit>
(error? try [pick at [1 2 3 4 5] 3 9223372036854775807])
; string
<64bit>
(error? try [pick at "12345" 3 -9223372036854775808])
(blank? pick at "12345" 3 -2147483648)
(blank? pick at "12345" 3 -2147483647)
(blank? pick at "12345" 3 -3)
(blank? pick at "12345" 3 -2)
(#"1" = pick at "12345" 3 -1)
[#857
    (#"2" = pick at "12345" 3 0)
]
(#"3" = pick at "12345" 3 1)
(#"4" = pick at "12345" 3 2)
(#"5" = pick at "12345" 3 3)
(blank? pick at "12345" 3 4)
(blank? pick at "12345" 3 2147483647)
<64bit>
(error? try [pick at "12345" 3 9223372036854775807])
