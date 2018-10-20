; functions/math/signq.r
(0 == sign-of 0)
(1 == sign-of 1)
(-1 == sign-of -1)
(1 == sign-of 2147483647)
(-1 == sign-of -2147483648)
; decimal
(0 == sign-of 0.0)
(1 == sign-of 4.94065645841247E-324)
(-1 == sign-of -4.94065645841247E-324)
(1 == sign-of 1.7976931348623157e308)
(-1 == sign-of -1.7976931348623157e308)
; money
(0 == sign-of $0)
(1 == sign-of $0.000000000000001)
(-1 == sign-of -$0.000000000000001)
; time
(0 == sign-of 0:00)
(1 == sign-of 0:00:0.000000001)
(-1 == sign-of -0:00:0.000000001)
