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

; !!! This test depends on having more precision in the pair scanner than
; atoi() does for integers.  r3-alpha only used atof(), so it was showing a
; floating point pair as if it were an integer.  Review making arbitrary
; decimal scans work--though this is a low priority.
;
; (2147483648x2147483648 = negate -2147483648x-2147483648)
; (2147483648x2147483648 = subtract 0x0 -2147483648x-2147483648)

[#1476
    (3 == pick 3x4 'x)
    (4 == pick 3x4 'y)
]
