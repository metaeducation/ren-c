; datatypes/tuple.r

(tuple? 1.2.3)
(not tuple? 1)
(tuple! = type of 1.2.3)
(1.2.3 = to tuple! [1 2 3])
("1.2.3" = mold 1.2.3)

; minimum
(tuple? make tuple! [])

; there is no longer a maximum (if it won't fit in a cell, it will allocate
; a series)

(tuple? 255.255.255.255.255.255.255)
(
    tuple: load "255.255.255.255.255.255.255.255.255.255.255"
    did all [
        11 = length of tuple
        (for i 1 11 1 [
            assert [tuple/(i) = 255]
        ] true)
    ]
)


; TO Conversion tests
(
    tests: [
        "a.b.c" [a b c]
        "a b c" [a b c]
        "1.2.3" [1 2 3]
        "1 2 3" [1 2 3]
    ]

    for-each [text structure] tests [
        tuple: ensure tuple! to tuple! text
        assert [(length of tuple) = (length of structure)]
        for i 1 (length of tuple) 1 [
            assert [tuple/(i) = structure/(i)]
        ]
    ]
    true
)
