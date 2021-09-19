; datatypes/percent.r
(percent? 0%)
(not percent? 1)
(percent! = type of 0%)
(percent? 0.0%)
(percent? 1%)
(percent? -1.0%)
(percent? 2.2%)
('bad-make-arg = (trap [make percent! 0]).id)
(0% = make percent! "0")
(0% = to percent! 0)
(0% = to percent! "0")
(100% = to percent! 1)
(10% = to percent! 0.1)
(error? trap [to percent! "t"])
(0 = to decimal! 0%)
(0.1 = to decimal! 10%)
(1.0 = to decimal! 100%)
(0% = load-value mold 0.0%)
(1% = load-value mold 1.0%)
(1.1% = load-value mold 1.1%)
(-1% = load-value mold -1.0%)
[#57
    (-5% = negate 5%)
]
[#57
    (10% = (5% + 5%))
]
[#57
    (6% = round 5.55%)
]
[#97
    ($59.0 = (10% * $590))
]
[#97
    ($100.6 = ($100 + 60%))
]
; 64-bit IEEE 754 maximum
; Minimal positive normalized
[#1475
    (same? 2.2250738585072014E-310% load-value mold/all 2.2250738585072014E-310%)
]
; Maximal positive denormalized
(same? 2.2250738585072009E-310% load-value mold/all 2.2250738585072009E-310%)
; Minimal positive denormalized
(same? 4.9406564584124654E-322% load-value mold/all 4.9406564584124654E-322%)
; Maximal negative normalized
(same? -2.2250738585072014E-306% load-value mold/all -2.2250738585072014E-306%)
; Minimal negative denormalized
(same? -2.2250738585072009E-306% load-value mold/all -2.2250738585072009E-306%)
; Maximal negative denormalized
(same? -4.9406564584124654E-322% load-value mold/all -4.9406564584124654E-322%)
(same? 10.000000000000001% load-value mold/all 10.000000000000001%)
(same? 29.999999999999999% load-value mold/all 29.999999999999999%)
(same? 30.000000000000004% load-value mold/all 30.000000000000004%)
(same? 9.9999999999999926e154% load-value mold/all 9.9999999999999926e154%)
; alternative form
(1.1% == 1,1%)
(110% = to percent! 110%)
(110% = to percent! "110%")
(1.1% = to percent! 1.1%)
(1.1% = to percent! "1.1%")
