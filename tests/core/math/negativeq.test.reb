; functions/math/negativeq.r
[not negative? 0]
[not negative? 1]
[negative? -1]
[not negative? 2147483647]
[negative? -2147483648]
#64bit
[not negative? 9223372036854775807]
#64bit
[negative? -9223372036854775808]
; decimal
[not negative? 0.0]
[not negative? 4.94065645841247E-324]
[negative? -4.94065645841247E-324]
[not negative? 1.7976931348623157e308]
[negative? -1.7976931348623157e308]
[not negative? $0]
[not negative? $0.01]
[negative? -$0.01]
[not negative? $999999999999999.87]
[negative? -$999999999999999.87]
; time
[not negative? 0:00]
[not negative? 0:00:0.000000001]
[negative? -0:00:0.000000001]
