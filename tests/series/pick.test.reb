; functions/series/pick.r
<64bit>
(error? trap [pick at [1 2 3 4 5] 3 -9223372036854775808])
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
(error? trap [pick at [1 2 3 4 5] 3 9223372036854775807])
; string
<64bit>
(error? trap [pick at "12345" 3 -9223372036854775808])
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
(error? trap [pick at "12345" 3 9223372036854775807])

[#2312 (
    data: [<a> <b>]
    did all [
        null = :data/(0.5)
        null = :data/(0.999999)
        <a> = data/(1.0)
        <a> = data/(1.5)
        <b> = data/(2.0)
        <b> = data/(2.000000001)
        null = :data/(3.0)
    ]
)]
[#2312 (
    data: "ab"
    did all [
        null = :data/(0.5)
        null = :data/(0.999999)
        #"a" = data/(1.0)
        #"a" = data/(1.5)
        #"b" = data/(2.0)
        #"b" = data/(2.000000001)
        null = :data/(3.0)
    ]
)]
