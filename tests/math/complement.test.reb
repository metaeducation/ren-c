; functions/math/complement.r


; integer
(-1 = complement 0)
(0 = complement -1)
(2147483647 = complement -2147483648)
(-2147483648 = complement 2147483647)
(255.255.255 = complement 0.0.0)
(0.0.0 = complement 255.255.255)
; binary
(#{ffffffffff} = complement #{0000000000})
(#{0000000000} = complement #{ffffffffff})
(not find complement charset "b" #"b")
(did find complement charset "a" #"b")
(
    a: make bitset! #{0000000000000000000000000000000000000000000000000000000000000000}
    a == complement complement a
)
(
    a: make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF}
    a == complement complement a
)
