; functions/math/complement.r
[#849
    (false = complement true)
]
(true = complement false)
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
(find complement charset "a" #"b")
(
    a: make bitset! #{0000000000000000000000000000000000000000000000000000000000000000}
    a == complement complement a
)
(
    a: make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF}
    a == complement complement a
)
; image
[#1706
    ((make image! [1x1 #{000000} #{00}]) = complement make image! [1x1 #{ffffff} #{ff}])
]
((make image! [1x1 #{ffffff} #{ff}]) = complement make image! [1x1 #{000000} #{00}])
; typeset
