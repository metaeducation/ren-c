; datatypes/block.r
[block? [1]]
[not block? 1]
[block! = type of [1]]

; minimum
[block? []]

; alternative literal representation
[[] == #[block! [[] 1]]]
[[] == make block! 0]
[[] == to block! ""]
["[]" == mold []]

[
    data: [a 10 b 20]
    did all [
        10 = data/a
        10 = select data 'a
        10 = select* data 'a
        20 = data/b
        20 = select data 'b
        20 = select* data 'b
        void? :a/c
        blank? select data 'c
        void? select* data 'c
    ]
]
