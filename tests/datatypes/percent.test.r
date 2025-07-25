; datatypes/percent.r
(percent? 0%)
(not percent? 1)
(percent! = type of 0%)
(percent? 0.0%)
(percent? 1%)
(percent? -1.0%)
(percent? 2.2%)

~bad-make-arg~ !! (
    make percent! 0
)

(0% = make percent! "0")
(0% = to percent! 0)
(0% = to percent! "0")
(100% = to percent! 100)
(10% = to percent! 10)
(warning? rescue [to percent! "t"])
(0.0 = to decimal! 0%)
(10.0 = to decimal! 10%)
(100.0 = to decimal! 100%)
(0% = transcode:one mold 0.0%)
(1% = transcode:one mold 1.0%)
(1.1% = transcode:one mold 1.1%)
(-1% = transcode:one mold -1.0%)
[#57
    (-5% = negate 5%)
]
[#57
    (10% = (5% + 5%))
]
[#57
    (6% = round 5.55%)
]

; 64-bit IEEE 754 maximum
; Minimal positive normalized
[#1475
    (same? 2.2250738585072014E-310% transcode:one mold 2.2250738585072014E-310%)
]
; Maximal positive denormalized
(same? 2.2250738585072009E-310% transcode:one mold 2.2250738585072009E-310%)
; Minimal positive denormalized
(same? 4.9406564584124654E-322% transcode:one mold 4.9406564584124654E-322%)
; Maximal negative normalized
(same? -2.2250738585072014E-306% transcode:one mold -2.2250738585072014E-306%)
; Minimal negative denormalized
(same? -2.2250738585072009E-306% transcode:one mold -2.2250738585072009E-306%)
; Maximal negative denormalized
(same? -4.9406564584124654E-322% transcode:one mold -4.9406564584124654E-322%)
(same? 10.000000000000001% transcode:one mold 10.000000000000001%)
(same? 29.999999999999999% transcode:one mold 29.999999999999999%)
(same? 30.000000000000004% transcode:one mold 30.000000000000004%)
(same? 9.9999999999999926e154% transcode:one mold 9.9999999999999926e154%)
; alternative form
(1.1% = 1,1%)
(110% = to percent! 110%)
(110% = to percent! "110%")
(1.1% = to percent! 1.1%)
(1.1% = to percent! "1.1%")
