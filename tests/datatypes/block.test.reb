; datatypes/block.r
(block? [1])
(not block? 1)
(block! = type of [1])

; minimum
(block? [])

; alternative literal representation
([] == #[block! [[] 1]])
([] == make block! 0)
([] == to block! "")
("[]" == mold [])

(
    data: [a 10 b 20]
    did all [
        10 = data/a
        10 = select data 'a
        10 = select* data 'a
        20 = data/b
        20 = select data 'b
        20 = select* data 'b
        void? :data/c
        blank? select data 'c
        void? select* data 'c
    ]
)

; #848
(
    [1] = copy/part tail [1] -2147483648
)
(
    e: trap [copy/part tail [1] -2147483649]
    error? e and (e/id = 'out-of-range)
)
(
    e: trap [[1] = copy/part tail of [1] -9223372036854775808]
    error? e and (e/id = 'out-of-range)
)
(
    e: trap [[] = copy/part [] 9223372036854775807]
    error? e and (e/id = 'out-of-range)
)
