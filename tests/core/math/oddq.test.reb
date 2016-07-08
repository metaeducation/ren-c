; functions/math/oddq.r
[not odd? 0]
[odd? 1]
[odd? -1]
[odd? 2147483647]
[not odd? -2147483648]
#64bit
[odd? 9223372036854775807]
#64bit
[not odd? -9223372036854775808]
; decimal
[not odd? 0.0]
[odd? 1.0]
[not odd? 2.0]
[odd? -1.0]
[not odd? -2.0]
[not odd? 1.7976931348623157e308]
[not odd? -1.7976931348623157e308]
; char
[not odd? #"^@"]
[odd? #"^a"]
[not odd? #"^b"]
[odd? #"^(ff)"]
; money
[not odd? $0]
[odd? $1]
[not odd? $2]
[odd? -$1]
[not odd? -$2]
[odd? $999999999999999]
[odd? -$999999999999999]
; time
[not odd? 0:00]
[not odd? 0:1:00]
[not odd? -0:1:00]
[odd? 0:0:01]
[not odd? 0:0:02]
[odd? -0:0:01]
[not odd? -0:0:02]
