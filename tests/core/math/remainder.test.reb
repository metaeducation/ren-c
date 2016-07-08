; functions/math/remainder.r
#64bit
; integer! tests
[0 = remainder -9223372036854775808 -1]
; integer! tests
[0 == remainder -2147483648 -1]
; time! tests
[-1:00 == remainder -1:00 -3:00]
[1:00 == remainder 1:00 -3:00]
