; functions/math/positiveq.r
(not positive? 0)
(positive? 1)
(not positive? -1)
(positive? 2147483647)
(not positive? -2147483648)
<64bit>
(positive? 9223372036854775807)
<64bit>
(not positive? -9223372036854775808)
; decimal
(not positive? 0.0)
(positive? 4.94065645841247E-324)
(not positive? -4.94065645841247E-324)
(positive? 1.7976931348623157e308)
(not positive? -1.7976931348623157e308)
(not positive? $0)
(positive? $0.01)
(not positive? -$0.01)
(positive? $999999999999999.87)
(not positive? -$999999999999999.87)
; time
(not positive? 0:00)
(positive? 0:00:0.000000001)
(not positive? -0:00:0.000000001)
