; datatypes/pair.r
[pair? 1x2]
[not pair? 1]
[pair! = type-of 1x2]
[1x1 = make pair! 1]
[1x2 = make pair! [1 2]]
[1x1 = to pair! 1]
; bug#17
[error? try [to pair! [0.4]]]
[1x2 = to pair! [1 2]]
["1x1" = mold 1x1]
; minimum
[pair? -2147483648x-2147483648]
; maximum
[pair? 2147483647x2147483647]
