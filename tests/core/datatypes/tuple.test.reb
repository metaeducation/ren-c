; datatypes/tuple.r
[tuple? 1.2.3]
[not tuple? 1]
[tuple! = type-of 1.2.3]
[1.2.3 = to tuple! [1 2 3]]
["1.2.3" = mold 1.2.3]
; minimum
[tuple? make tuple! []]
; maximum
[tuple? 255.255.255.255.255.255.255]
[error? try [load "255.255.255.255.255.255.255.255.255.255.255"]]
