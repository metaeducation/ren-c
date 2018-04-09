; functions/math/negate.r
(0 = negate 0)
(-1 = negate 1)
(1 = negate -1)
<32bit>
(error? try [negate -2147483648])
; decimal
(0.0 == negate 0.0)
(-1.0 == negate 1.0)
(1.0 == negate -1.0)
(1.7976931348623157e308 = negate -1.7976931348623157e308)
(-1.7976931348623157e308 = negate 1.7976931348623157e308)
(4.94065645841247E-324 = negate -4.94065645841247E-324)
(-4.94065645841247E-324 = negate 4.94065645841247E-324)
; pair
(0x0 = negate 0x0)
(-1x-1 = negate 1x1)
(1x1 = negate -1x-1)
(-1x1 = negate 1x-1)
; money
($0 = negate $0)
(-$1 = negate $1)
($1 = negate -$1)
; time
(0:00 = negate 0:00)
(-1:01 = negate 1:01)
(1:01 = negate -1:01)
; bitset
(
    a: make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF}
    a == negate negate a
)
(
    a: make bitset! #{0000000000000000000000000000000000000000000000000000000000000000}
    a == negate negate a
)
