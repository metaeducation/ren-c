; datatypes/pair.r
(pair? 1x2)
(not pair? 1)
(pair! = type of 1x2)
(1x1 = make pair! 1)
(1x2 = make pair! [1 2])
(1x1 = to pair! 1)
[#17
    (error? trap [to pair! [0.4]])
]
(1x2 = to pair! [1 2])
("1x1" = mold 1x1)
; minimum
(pair? -2147483648x-2147483648)
; maximum
(pair? 2147483647x2147483647)

; Original R3-Alpha code used `float` for pairs.  R3C prototype implementation
; uses actual pairing of DECIMAL! or INTEGER!.  The compare routines are
; still based on `double` coercion, this test is out of range, punt on it.
;
;(2147483648x2147483648 = negate -2147483648x-2147483648)

; Ren-C preserves the INTEGER!-ness or DECIMAL-ness of the components of pair
;
[#1476
    (3 == pick 3x4 'x)
    (4 == pick 3x4 'y)

    (3 == pick 3x4.0 'x)
    (4.0 == pick 3x4.0 'y)

    (3.0 == pick 3.0x4 'x)
    (4 == pick 3.0x4 'y)

    (3.0 == pick 3.0x4.0 'x)
    (4.0 == pick 3.0x4.0 'y)
]
