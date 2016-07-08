; datatypes/decimal.r
[decimal? 0.0]
[not decimal? 0]
[decimal! = type-of 0.0]
[decimal? 1.0]
[decimal? -1.0]
[decimal? 1.5]
; LOAD decimal and to binary! tests
; 64-bit IEEE 754 maximum
[equal? #{7FEFFFFFFFFFFFFF} to binary! 1.7976931348623157e308]
; Minimal positive normalized
[equal? #{0010000000000000} to binary! 2.2250738585072014E-308]
; Maximal positive denormalized
[equal? #{000FFFFFFFFFFFFF} to binary! 2.225073858507201E-308]
; Minimal positive denormalized
[equal? #{0000000000000001} to binary! 4.9406564584124654E-324]
; zero
[equal? #{0000000000000000} to binary! 0.0]
; negative zero
[equal? #{8000000000000000} to binary! -0.0]
; Maximal negative denormalized
[equal? #{8000000000000001} to binary! -4.9406564584124654E-324]
; Minimal negative denormalized
[equal? #{800FFFFFFFFFFFFF} to binary! -2.225073858507201E-308]
; Maximal negative normalized
[equal? #{8010000000000000} to binary! -2.2250738585072014E-308]
; 64-bit IEEE 754 minimum
[equal? #{FFEFFFFFFFFFFFFF} to binary! -1.7976931348623157e308]
; bug#729
; MOLD decimal accuracy tests
[
    system/options/decimal-digits: 17
    system/options/decimal-digits = 17
]
; 64-bit IEEE 754 maximum
[zero? 1.7976931348623157e308 - load mold 1.7976931348623157e308]
[same? 1.7976931348623157e308 load mold 1.7976931348623157e308]
; Minimal positive normalized
[zero? 2.2250738585072014E-308 - load mold 2.2250738585072014E-308]
[same? 2.2250738585072014E-308 load mold 2.2250738585072014E-308]
; Maximal positive denormalized
[zero? 2.225073858507201E-308 - load mold 2.225073858507201E-308]
[same? 2.225073858507201E-308 load mold 2.225073858507201E-308]
; Minimal positive denormalized
[zero? 4.9406564584124654E-324 - load mold 4.9406564584124654E-324]
[same? 4.9406564584124654E-324 load mold 4.9406564584124654E-324]
; Positive zero
[zero? 0.0 - load mold 0.0]
[same? 0.0 load mold 0.0]
; Negative zero
[zero? -0.0 - load mold -0.0]
[same? -0.0 load mold -0.0]
; Maximal negative denormalized
[zero? -4.9406564584124654E-324 - load mold -4.9406564584124654E-324]
[same? -4.9406564584124654E-324 load mold -4.9406564584124654E-324]
; Minimal negative denormalized
[zero? -2.225073858507201E-308 - load mold -2.225073858507201E-308]
[same? -2.225073858507201E-308 load mold -2.225073858507201E-308]
; Maximal negative normalized
[zero? -2.2250738585072014E-308 - load mold -2.2250738585072014E-308]
[same? -2.2250738585072014E-308 load mold -2.2250738585072014E-308]
; 64-bit IEEE 754 minimum
[zero? -1.7976931348623157E308 - load mold -1.7976931348623157e308]
[same? -1.7976931348623157E308 load mold -1.7976931348623157e308]
[zero? 0.10000000000000001 - load mold 0.10000000000000001]
[same? 0.10000000000000001 load mold 0.10000000000000001]
[zero? 0.29999999999999999 - load mold 0.29999999999999999]
[same? 0.29999999999999999 load mold 0.29999999999999999]
[zero? 0.30000000000000004 - load mold 0.30000000000000004]
[same? 0.30000000000000004 load mold 0.30000000000000004]
[zero? 9.9999999999999926e152 - load mold 9.9999999999999926e152]
[same? 9.9999999999999926e152 load mold 9.9999999999999926e152]
; bug#718
[
    a: 9.9999999999999926e152 * 1e-138
    zero? a - load mold a
]
; bug#897
; MOLD/ALL decimal accuracy tests
; 64-bit IEEE 754 maximum
[zero? 1.7976931348623157e308 - load mold/all 1.7976931348623157e308]
[same? 1.7976931348623157e308 load mold/all 1.7976931348623157e308]
; Minimal positive normalized
[zero? 2.2250738585072014E-308 - load mold/all 2.2250738585072014E-308]
[same? 2.2250738585072014E-308 load mold/all 2.2250738585072014E-308]
; Maximal positive denormalized
[zero? 2.225073858507201E-308 - load mold/all 2.225073858507201E-308]
[same? 2.225073858507201E-308 load mold/all 2.225073858507201E-308]
; Minimal positive denormalized
[zero? 4.9406564584124654E-324 - load mold/all 4.9406564584124654E-324]
[same? 4.9406564584124654E-324 load mold/all 4.9406564584124654E-324]
; Positive zero
[zero? 0.0 - load mold/all 0.0]
[same? 0.0 load mold/all 0.0]
; Negative zero
[zero? -0.0 - load mold/all -0.0]
[same? -0.0 load mold/all -0.0]
; Maximal negative denormalized
[zero? -4.9406564584124654E-324 - load mold/all -4.9406564584124654E-324]
[same? -4.9406564584124654E-324 load mold/all -4.9406564584124654E-324]
; Minimal negative denormalized
[zero? -2.225073858507201E-308 - load mold/all -2.225073858507201E-308]
[same? -2.225073858507201E-308 load mold/all -2.225073858507201E-308]
; Maximal negative normalized
[zero? -2.2250738585072014E-308 - load mold/all -2.2250738585072014E-308]
[same? -2.2250738585072014E-308 load mold/all -2.2250738585072014E-308]
; 64-bit IEEE 754 minimum
[zero? -1.7976931348623157E308 - load mold/all -1.7976931348623157e308]
[same? -1.7976931348623157E308 load mold/all -1.7976931348623157e308]
[zero? 0.10000000000000001 - load mold/all 0.10000000000000001]
[same? 0.10000000000000001 load mold/all 0.10000000000000001]
[zero? 0.29999999999999999 - load mold/all 0.29999999999999999]
[same? 0.29999999999999999 load mold/all 0.29999999999999999]
[zero? 0.30000000000000004 - load mold/all 0.30000000000000004]
[same? 0.30000000000000004 load mold/all 0.30000000000000004]
[zero? 9.9999999999999926e152 - load mold/all 9.9999999999999926e152]
[same? 9.9999999999999926e152 load mold/all 9.9999999999999926e152]
; LOAD decimal accuracy tests
[equal? to binary! 2.2250738585072004e-308 #{000FFFFFFFFFFFFE}]
[equal? to binary! 2.2250738585072005e-308 #{000FFFFFFFFFFFFE}]
[equal? to binary! 2.2250738585072006e-308 #{000FFFFFFFFFFFFE}]
[equal? to binary! 2.2250738585072007e-308 #{000FFFFFFFFFFFFF}]
[equal? to binary! 2.2250738585072008e-308 #{000FFFFFFFFFFFFF}]
[equal? to binary! 2.2250738585072009e-308 #{000FFFFFFFFFFFFF}]
[equal? to binary! 2.225073858507201e-308 #{000FFFFFFFFFFFFF}]
[equal? to binary! 2.2250738585072011e-308 #{000FFFFFFFFFFFFF}]
[equal? to binary! 2.2250738585072012e-308 #{0010000000000000}]
[equal? to binary! 2.2250738585072013e-308 #{0010000000000000}]
[equal? to binary! 2.2250738585072014e-308 #{0010000000000000}]
; bug#1753
[c: last mold/all 1e16 (#"0" <= c) and* (#"9" >= c)]
; alternative form
[1.1 == 1,1]
[1.1 = make decimal! 1.1]
[1.1 = make decimal! "1.1"]
[1.1 = to decimal! 1.1]
[1.1 = to decimal! "1.1"]
[error? try [to decimal! "t"]]
; decimal! to binary! and binary! to decimal!
[equal? #{3ff0000000000000} to binary! 1.0]
[same? to decimal! #{3ff0000000000000} 1.0]
; bug#747
[equal? #{3FF0000000000009} to binary! to decimal! #{3FF0000000000009}]
