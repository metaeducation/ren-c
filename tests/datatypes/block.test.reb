; datatypes/block.r
(block? [1])
(not block? 1)
(block! == type of [1])

; minimum
(block? [])

; alternative literal representation
([] == #[block! [[] 1]])
([] == make block! 0)
([] == make block! "")
("[]" == mold [])

(
    data: [a 10 b 20]
    did all [
        10 == data/a
        10 == select data 'a
        20 == data/b
        20 == select data 'b
        not set? 'data/c
        null? select data 'c
    ]
)

; #848
(
    [1] == copy/part tail [1] -2147483648
)
(
    e: trap [copy/part tail [1] -2147483649]
    error? e and [e/id is 'Out-Of-Range]
)
(
    e: trap [[1] == copy/part tail of [1] -9223372036854775808]
    error? e and [e/id is 'Out-Of-Range]
)
(
    e: trap [[] == copy/part [] 9223372036854775807]
    error? e and [e/id is 'Out-Of-Range]
)
