; functions/math/absolute.r
[:abs = :absolute]
[0 = abs 0]
[1 = abs 1]
[1 = abs -1]
[2147483647 = abs 2147483647]
[2147483647 = abs -2147483647]
[0.0 = abs 0.0]
[zero? 1.0 - abs 1.0]
[zero? 1.0 - abs -1.0]
; simple tests verify correct args and refinements; integer tests
#64bit
[9223372036854775807 = abs 9223372036854775807]
#64bit
[9223372036854775807 = abs -9223372036854775807]
; pair! tests
[0x0 = abs 0x0]
[0x1 = abs 0x1]
[1x0 = abs 1x0]
[1x1 = abs 1x1]
[0x1 = abs 0x-1]
[1x0 = abs -1x0]
[1x1 = abs -1x-1]
[2147483647x2147483647 = abs 2147483647x2147483647]
[2147483647x2147483647 = abs 2147483647x-2147483647]
[2147483647x2147483647 = abs -2147483647x2147483647]
[2147483647x2147483647 = abs -2147483647x-2147483647]
; bug#833
#64bit
[
    a: try [abs to integer! #{8000000000000000}]
    any [error? a not negative? a]
]
