; functions/context/bind.r

(
    e: trap [do make block! ":a"]
    e/id = 'not-bound
)

[#50
    (null? binding of to word! "zzz")
]
; BIND works 'as expected' in object spec
[#1549 (
    b1: [self]
    ob: make object! [
        b2: [self]
        set 'a same? first b2 first bind/copy b1 'b2
    ]
    a
)]
; BIND works 'as expected' in function body
[#1549 (
    b1: [self]
    f: func [/local b2] [
        b2: [self]
        same? first b2 first bind/copy b1 'b2
    ]
    f
)]
; BIND works 'as expected' in REPEAT body
[#1549 (
    b1: [self]
    repeat i 1 [
        b2: [self]
        same? first b2 first bind/copy b1 'i
    ]
)]
[#1655
    (not head? bind next [1] 'rebol)
]
[#892 #216
    (y: 'x eval func [<local> x] [x: true get bind y 'x])
]

[#2086 (
    bind next block: [a a] use [a] ['a]
    same? 'a first block
)]

[#1893 (
    word: eval func [x] ['x] 1
    same? word bind 'x word
)]
