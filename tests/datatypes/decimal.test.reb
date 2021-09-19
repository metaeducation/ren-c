; datatypes/decimal.r
(decimal? 0.0)
(not decimal? 0)
(decimal! = type of 0.0)
(decimal? 1.0)
(decimal? -1.0)
(decimal? 1.5)

; LOAD decimal and to binary! tests
; 64-bit IEEE 754 maximum
(equal? #{7FEFFFFFFFFFFFFF} to binary! 1.7976931348623157e308)
; Minimal positive normalized
(equal? #{0010000000000000} to binary! 2.2250738585072014E-308)
; Maximal positive denormalized
(equal? #{000FFFFFFFFFFFFF} to binary! 2.225073858507201E-308)
; Minimal positive denormalized
(equal? #{0000000000000001} to binary! 4.9406564584124654E-324)
; zero
(equal? #{0000000000000000} to binary! 0.0)
; negative zero
(equal? #{8000000000000000} to binary! -0.0)
; Maximal negative denormalized
(equal? #{8000000000000001} to binary! -4.9406564584124654E-324)
; Minimal negative denormalized
(equal? #{800FFFFFFFFFFFFF} to binary! -2.225073858507201E-308)
; Maximal negative normalized
(equal? #{8010000000000000} to binary! -2.2250738585072014E-308)
; 64-bit IEEE 754 minimum
(equal? #{FFEFFFFFFFFFFFFF} to binary! -1.7976931348623157e308)

; MOLD decimal accuracy tests
[#729 (
    system/options/decimal-digits: 17
    system/options/decimal-digits = 17
)]

; 64-bit IEEE 754 maximum
(zero? 1.7976931348623157e308 - load-value mold 1.7976931348623157e308)
(same? 1.7976931348623157e308 load-value mold 1.7976931348623157e308)

; Minimal positive normalized
(zero? 2.2250738585072014E-308 - load-value mold 2.2250738585072014E-308)
(same? 2.2250738585072014E-308 load-value mold 2.2250738585072014E-308)
; Maximal positive denormalized
(zero? 2.225073858507201E-308 - load-value mold 2.225073858507201E-308)
(same? 2.225073858507201E-308 load-value mold 2.225073858507201E-308)
; Minimal positive denormalized
(zero? 4.9406564584124654E-324 - load-value mold 4.9406564584124654E-324)
(same? 4.9406564584124654E-324 load-value mold 4.9406564584124654E-324)
; Positive zero
(zero? 0.0 - load-value mold 0.0)
(same? 0.0 load-value mold 0.0)
; Negative zero
(zero? -0.0 - load-value mold -0.0)
(same? -0.0 load-value mold -0.0)
; Maximal negative denormalized
(zero? -4.9406564584124654E-324 - load-value mold -4.9406564584124654E-324)
(same? -4.9406564584124654E-324 load-value mold -4.9406564584124654E-324)
; Minimal negative denormalized
(zero? -2.225073858507201E-308 - load-value mold -2.225073858507201E-308)
(same? -2.225073858507201E-308 load-value mold -2.225073858507201E-308)
; Maximal negative normalized
(zero? -2.2250738585072014E-308 - load-value mold -2.2250738585072014E-308)
(same? -2.2250738585072014E-308 load-value mold -2.2250738585072014E-308)
; 64-bit IEEE 754 minimum
(zero? -1.7976931348623157E308 - load-value mold -1.7976931348623157e308)
(same? -1.7976931348623157E308 load-value mold -1.7976931348623157e308)
(zero? 0.10000000000000001 - load-value mold 0.10000000000000001)
(same? 0.10000000000000001 load-value mold 0.10000000000000001)
(zero? 0.29999999999999999 - load-value mold 0.29999999999999999)
(same? 0.29999999999999999 load-value mold 0.29999999999999999)
(zero? 0.30000000000000004 - load-value mold 0.30000000000000004)
(same? 0.30000000000000004 load-value mold 0.30000000000000004)
(zero? 9.9999999999999926e152 - load-value mold 9.9999999999999926e152)
(same? 9.9999999999999926e152 load-value mold 9.9999999999999926e152)

[#718 (
    a: 9.9999999999999926e152 * 1e-138
    zero? a - load-value mold a
)]

; MOLD/ALL decimal accuracy tests
; 64-bit IEEE 754 maximum
[#897
    (zero? 1.7976931348623157e308 - load-value mold/all 1.7976931348623157e308)
]
(same? 1.7976931348623157e308 load-value mold/all 1.7976931348623157e308)
; Minimal positive normalized
(zero? 2.2250738585072014E-308 - load-value mold/all 2.2250738585072014E-308)
(same? 2.2250738585072014E-308 load-value mold/all 2.2250738585072014E-308)
; Maximal positive denormalized
(zero? 2.225073858507201E-308 - load-value mold/all 2.225073858507201E-308)
(same? 2.225073858507201E-308 load-value mold/all 2.225073858507201E-308)
; Minimal positive denormalized
(zero? 4.9406564584124654E-324 - load-value mold/all 4.9406564584124654E-324)
(same? 4.9406564584124654E-324 load-value mold/all 4.9406564584124654E-324)
; Positive zero
(zero? 0.0 - load-value mold/all 0.0)
(same? 0.0 load-value mold/all 0.0)
; Negative zero
(zero? -0.0 - load-value mold/all -0.0)
(same? -0.0 load-value mold/all -0.0)
; Maximal negative denormalized
(zero? -4.9406564584124654E-324 - load-value mold/all -4.9406564584124654E-324)
(same? -4.9406564584124654E-324 load-value mold/all -4.9406564584124654E-324)
; Minimal negative denormalized
(zero? -2.225073858507201E-308 - load-value mold/all -2.225073858507201E-308)
(same? -2.225073858507201E-308 load-value mold/all -2.225073858507201E-308)
; Maximal negative normalized
(zero? -2.2250738585072014E-308 - load-value mold/all -2.2250738585072014E-308)
(same? -2.2250738585072014E-308 load-value mold/all -2.2250738585072014E-308)
; 64-bit IEEE 754 minimum
(zero? -1.7976931348623157E308 - load-value mold/all -1.7976931348623157e308)
(same? -1.7976931348623157E308 load-value mold/all -1.7976931348623157e308)
(zero? 0.10000000000000001 - load-value mold/all 0.10000000000000001)
(same? 0.10000000000000001 load-value mold/all 0.10000000000000001)
(zero? 0.29999999999999999 - load-value mold/all 0.29999999999999999)
(same? 0.29999999999999999 load-value mold/all 0.29999999999999999)
(zero? 0.30000000000000004 - load-value mold/all 0.30000000000000004)
(same? 0.30000000000000004 load-value mold/all 0.30000000000000004)
(zero? 9.9999999999999926e152 - load-value mold/all 9.9999999999999926e152)
(same? 9.9999999999999926e152 load-value mold/all 9.9999999999999926e152)

; LOAD decimal accuracy tests
(equal? to binary! 2.2250738585072004e-308 #{000FFFFFFFFFFFFE})
(equal? to binary! 2.2250738585072005e-308 #{000FFFFFFFFFFFFE})
(equal? to binary! 2.2250738585072006e-308 #{000FFFFFFFFFFFFE})
(equal? to binary! 2.2250738585072007e-308 #{000FFFFFFFFFFFFF})
(equal? to binary! 2.2250738585072008e-308 #{000FFFFFFFFFFFFF})
(equal? to binary! 2.2250738585072009e-308 #{000FFFFFFFFFFFFF})
(equal? to binary! 2.225073858507201e-308 #{000FFFFFFFFFFFFF})
(equal? to binary! 2.2250738585072011e-308 #{000FFFFFFFFFFFFF})
(equal? to binary! 2.2250738585072012e-308 #{0010000000000000})
(equal? to binary! 2.2250738585072013e-308 #{0010000000000000})
(equal? to binary! 2.2250738585072014e-308 #{0010000000000000})

[#1753 (
    c: last mold/all 1e16
    (#0 <= c) and (#9 >= c)
)]

; alternative form
(1.1 == 1,1)
(1.1 = make decimal! "1.1")
('bad-make-arg = (trap [make decimal! 1.1]).id)
(1.1 = to decimal! 1.1)
(1.1 = to decimal! "1.1")
(error? trap [to decimal! "t"])

; decimal! to binary! and binary! to decimal!
(equal? #{3ff0000000000000} to binary! 1.0)
(same? to decimal! #{3ff0000000000000} 1.0)

[#747 (
    equal? #{3FF0000000000009} to binary! to decimal! #{3FF0000000000009}
)]

; TO DECIMAL! of 2-element INTEGER! PATH! treats as fraction
[
    (0.5 = to decimal! 1/2)
    ('bad-cast = (trap [to decimal! 1/2/3]).id)
    ('zero-divide = (trap [to decimal! 1/0]).id)
]

; Unsupported experiment: MAKE DECIMAL! of 2-element path run DIVIDE code
; (Don't depend on this, not very useful since you have to quote anyway)
[
    (0.175 = make decimal! '(50% + 20%)/(1 + 3))
]
