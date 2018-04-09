; functions/math/zeroq.r
(zero? 0)
(not zero? 1)
(not zero? -1)
(not zero? 2147483647)
(not zero? -2147483648)
<64bit>
(not zero? 9223372036854775807)
<64bit>
(not zero? -9223372036854775808)
; decimal
(zero? 0.0)
(not zero? 1.7976931348623157e308)
(not zero? -1.7976931348623157e308)
; pair
(zero? 0x0)
(not zero? 1x0)
(not zero? -1x0)
(not zero? 2147483647x0)
(not zero? -2147483648x0)
(not zero? 0x1)
(not zero? 0x-1)
(not zero? 0x2147483647)
(not zero? 0x-2147483648)
; char
(zero? #"^@")
(not zero? #"^a")
(not zero? #"^(ff)")
; money
(zero? $0)
(not zero? $0.01)
(not zero? -$0.01)
(not zero? $999999999999999.87)
(not zero? -$999999999999999.87)
(zero? negate $0)
; time
(zero? 0:00)
(not zero? 0:00:0.000000001)
(not zero? -0:00:0.000000001)
; tuple
(zero? 0.0.0)
(not zero? 1.0.0)
(not zero? 255.0.0)
(not zero? 0.1.0)
(not zero? 0.255.0)
(not zero? 0.0.1)
(not zero? 0.0.255)
