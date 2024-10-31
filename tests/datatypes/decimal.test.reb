; datatypes/decimal.r
(decimal? 0.0)
(not decimal? 0)
(decimal! = type of 0.0)
(decimal? 1.0)
(decimal? -1.0)
(decimal? 1.5)

; LOAD decimal and to binary! tests

; 64-bit IEEE 754 maximum
(
    for-each [bin num] [
        #{7FEFFFFFFFFFFFFF} 1.7976931348623157e308  ; max
        #{0010000000000000} 2.2250738585072014E-308  ; min (+) normalized
        #{000FFFFFFFFFFFFF} 2.225073858507201E-308  ; max (+) denormalized
        #{0000000000000001} 4.9406564584124654E-324  ; min (+) denormalized
        #{3ff0000000000000} 1.0  ; one
        #{0000000000000000} 0.0  ; zero
        #{8000000000000000} -0.0  ; negative zero
        #{8000000000000001} -4.9406564584124654E-324  ; max (-) denormalized
        #{800FFFFFFFFFFFFF} -2.225073858507201E-308  ; min (-) denormalized
        #{8010000000000000} -2.2250738585072014E-308  ; max (-) normalized
        #{FFEFFFFFFFFFFFFF} -1.7976931348623157e308  ; min

        ; accuracy tests
        #{3FF0000000000009} 1.000000000000002  ; #747
        #{000FFFFFFFFFFFFE} 2.2250738585072004e-308
        #{000FFFFFFFFFFFFE} 2.2250738585072005e-308
        #{000FFFFFFFFFFFFE} 2.2250738585072006e-308
        #{000FFFFFFFFFFFFF} 2.2250738585072007e-308
        #{000FFFFFFFFFFFFF} 2.2250738585072008e-308
        #{000FFFFFFFFFFFFF} 2.2250738585072009e-308
        #{000FFFFFFFFFFFFF} 2.225073858507201e-308
        #{000FFFFFFFFFFFFF} 2.2250738585072011e-308
        #{0010000000000000} 2.2250738585072012e-308
        #{0010000000000000} 2.2250738585072013e-308
        #{0010000000000000} 2.2250738585072014e-308
    ][
        if bin != encode 'IEEE-754 num [
            fail ["IEEE-754 encoding of" num "was not" @bin]
        ]
        if num != decode 'IEEE-754 bin [
            fail ["IEEE-754 decoding of" @bin "was not" num]
        ]
    ]
    ok
)

; #1134 "decimal tolerance"
(not same? // [
    decode 'IEEE-754 #{3FD3333333333333}
    decode 'IEEE-754 #{3FD3333333333334}
])
(not same? // [
    decode 'IEEE-754 #{3FB9999999999999}
    decode 'IEEE-754 #{3FB999999999999A}
])

; #1134 "decimal tolerance"
(not strict-equal? // [
    decode 'IEEE-754 #{3FD3333333333333}
    decode 'IEEE-754 #{3FD3333333333334}
])
(not strict-equal? // [
    decode 'IEEE-754 #{3FB9999999999999}
    decode 'IEEE-754 #{3FB999999999999A}
])


; 64-bit IEEE 754 maximum
(zero? 1.7976931348623157e308 - transcode:one mold 1.7976931348623157e308)
(same? 1.7976931348623157e308 transcode:one mold 1.7976931348623157e308)

; Minimal positive normalized
(zero? 2.2250738585072014E-308 - transcode:one mold 2.2250738585072014E-308)
(same? 2.2250738585072014E-308 transcode:one mold 2.2250738585072014E-308)
; Maximal positive denormalized
(zero? 2.225073858507201E-308 - transcode:one mold 2.225073858507201E-308)
(same? 2.225073858507201E-308 transcode:one mold 2.225073858507201E-308)
; Minimal positive denormalized
(zero? 4.9406564584124654E-324 - transcode:one mold 4.9406564584124654E-324)
(same? 4.9406564584124654E-324 transcode:one mold 4.9406564584124654E-324)
; Positive zero
(zero? 0.0 - transcode:one mold 0.0)
(same? 0.0 transcode:one mold 0.0)
; Negative zero
(zero? -0.0 - transcode:one mold -0.0)
(same? -0.0 transcode:one mold -0.0)
; Maximal negative denormalized
(zero? -4.9406564584124654E-324 - transcode:one mold -4.9406564584124654E-324)
(same? -4.9406564584124654E-324 transcode:one mold -4.9406564584124654E-324)
; Minimal negative denormalized
(zero? -2.225073858507201E-308 - transcode:one mold -2.225073858507201E-308)
(same? -2.225073858507201E-308 transcode:one mold -2.225073858507201E-308)
; Maximal negative normalized
(zero? -2.2250738585072014E-308 - transcode:one mold -2.2250738585072014E-308)
(same? -2.2250738585072014E-308 transcode:one mold -2.2250738585072014E-308)
; 64-bit IEEE 754 minimum
(zero? -1.7976931348623157E308 - transcode:one mold -1.7976931348623157e308)
(same? -1.7976931348623157E308 transcode:one mold -1.7976931348623157e308)
(zero? 0.10000000000000001 - transcode:one mold 0.10000000000000001)
(same? 0.10000000000000001 transcode:one mold 0.10000000000000001)
(zero? 0.29999999999999999 - transcode:one mold 0.29999999999999999)
(same? 0.29999999999999999 transcode:one mold 0.29999999999999999)
(zero? 0.30000000000000004 - transcode:one mold 0.30000000000000004)
(same? 0.30000000000000004 transcode:one mold 0.30000000000000004)
(zero? 9.9999999999999926e152 - transcode:one mold 9.9999999999999926e152)
(same? 9.9999999999999926e152 transcode:one mold 9.9999999999999926e152)

[#718 (
    a: 9.9999999999999926e152 * 1e-138
    zero? a - transcode:one mold a
)]


[#1753 (
    c: last mold 1e16
    (#0 <= c) and (#9 >= c)
)]

; alternative form
(1.1 == 1,1)
(1.1 = to decimal! "1.1")

~bad-make-arg~ !! (
    make decimal! 1.1
)

(1.1 = to decimal! 1.1)
(1.1 = to decimal! "1.1")
(error? trap [to decimal! "t"])

; Experiment: MAKE DECIMAL! of 2-element INTEGER! PATH! treats as fraction
[
    (0.5 = make decimal! 1/2)
    ~???~ !! (make decimal! 1/2/3)
    ~zero-divide~ !! (make decimal! 1/0)
    (0.175 = make decimal! '(50% + 20%)/(1 + 3))
]
