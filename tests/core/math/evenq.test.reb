; functions/math/evenq.r
[even? 0]
[not even? 1]
[not even? -1]
[not even? 2147483647]
[even? -2147483648]
#64bit
[not even? 9223372036854775807]
#64bit
[even? -9223372036854775808]
; decimal
[even? 0.0]
[not even? 1.0]
[even? 2.0]
[not even? -1.0]
[even? -2.0]
; bug#1775
[even? 1.7976931348623157e308]
[even? -1.7976931348623157e308]
; char
[even? #"^@"]
[not even? #"^a"]
[even? #"^b"]
[not even? #"^(ff)"]
; money
[even? $0]
[not even? $1]
[even? $2]
[not even? -$1]
[even? -$2]
[not even? $999999999999999]
[not even? -$999999999999999]
; time
[even? 0:00]
[even? 0:1:00]
[even? -0:1:00]
[not even? 0:0:01]
[even? 0:0:02]
[not even? -0:0:01]
[even? -0:0:02]
