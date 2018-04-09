; datatypes/money.r
(money? $0.0)
(not money? 0)
(money! = type of $0.0)
(money? $1.0)
(money? -$1.0)
(money? $1.5)
; moldable maximum for R2
(money? $999999999999999.87)
; moldable minimum for R2
(money? -$999999999999999.87)
; check, whether these are moldable
(
    x: $999999999999999
    did any [
        error? try [x: x + $1]
        not error? try [mold x]
    ]
)
(
    x: -$999999999999999
    did any [
        error? try [x: x - $1]
        not error? try [mold x]
    ]
)
; alternative form
($1.1 == $1,1)
(
    did any [
        error? try [x: $1234567890123456]
        not error? try [mold x]
    ]
)
($11 = make money! 11)
($1.1 = make money! "1.1")
[#4
    ($11 = to money! 11)
]
($1.1 = to money! "1.1")
("$1.10" = mold $1.10)
("-$1.10" = mold -$1.10)
("$0" = mold $0)
; equality
($1 = $1.0000000000000000000000000)
(not ($1 = $2))
; maximum for R3
(equal? $99999999999999999999999999e127 $99999999999999999999999999e127)
; minimum for R3
(equal? -$99999999999999999999999999e127 -$99999999999999999999999999e127)
(not ($0 = $1e-128))
(not ($0 = -$1e-128))
; inequality
(not ($1 <> $1))
($1 <= $2)
(not ($2 <= $1))
(not zero? $1e-128)
(not zero? -$1e-128)
; positive? tests
(not positive? negate $0)
(positive? $1e-128)
(not positive? -$1e-128)
(not negative? negate $0)
(not negative? $1e-128)
(negative? -$1e-128)
; same? tests
(same? $0 $0)
(same? $0 negate $0)
(same? $1 $1)
(not same? $1 $1.0)
("$1.0000000000000000000000000" = mold $2.0000000000000000000000000 - $1)
("$1" = mold $2 - $1)
("$1" = mold $1 * $1)
("$4" = mold $2 * $2)
("$1.0000000000000000000000000" = mold $1 * $1.0000000000000000000000000)
("$1.0000000000000000000000000" = mold $1.0000000000000000000000000 * $1.0000000000000000000000000)
; division uses "full precision"
("$1.0000000000000000000000000" = mold $1 / $1)
("$1.0000000000000000000000000" = mold $1 / $1.0)
("$1.0000000000000000000000000" = mold $1 / $1.000)
("$1.0000000000000000000000000" = mold $1 / $1.000000)
("$1.0000000000000000000000000" = mold $1 / $1.000000000)
("$1.0000000000000000000000000" = mold $1 / $1.000000000000)
("$1.0000000000000000000000000" = mold $1 / $1.0000000000000000000000000)
("$0.10000000000000000000000000" = mold $1 / $10)
("$0.33333333333333333333333333" = mold $1 / $3)
("$0.66666666666666666666666667" = mold $2 / $3)
; conversion to integer
(1 = to integer! $1)
<64bit>
(-9223372036854775808 == to integer! -$9223372036854775808.99)
<64bit>
(9223372036854775807 == to integer! $9223372036854775807.99)
; conversion to decimal
(1.0 = to decimal! $1)
(zero? 0.3 - to decimal! $0.3)
(zero? 0.1 - to decimal! $0.1)
(
    x: 9.9999999999999981e152
    zero? x - to decimal! to money! x
)
(
    x: -9.9999999999999981e152
    zero? x - to decimal! to money! x
)
(
    x: 9.9999999999999926E152
    zero? x - to decimal! to money! x
)
(
    x: -9.9999999999999926E152
    zero? x - to decimal! to money! x
)
(
    x: 9.9999999999999293E152
    zero? x - to decimal! to money! x
)
(
    x: -9.9999999999999293E152
    zero? x - to decimal! to money! x
)
(
    x: to decimal! $1e-128
    zero? x - to decimal! to money! x
)
(
    x: to decimal! -$1e-128
    zero? x - to decimal! to money! x
)
(
    x: 9.2233720368547758E18
    zero? x - to decimal! to money! x
)
(
    x: -9.2233720368547758E18
    zero? x - to decimal! to money! x
)
(
    x: 9.2233720368547748E18
    zero? x - to decimal! to money! x
)
(
    x: -9.2233720368547748E18
    zero? x - to decimal! to money! x
)
(
    x: 9.2233720368547779E18
    zero? x - to decimal! to money! x
)
(
    x: -9.2233720368547779E18
    zero? x - to decimal! to money! x
)

(
    $0.0 == (
        $0.000'000'000'000'001 - round/even/to $0.000'000'000'000'001'1 1e-15
    )
)

(
    not negative?
        1e-31
            - abs (to money! 26e-17)
            - round/even/to $0.000'000'000'000'000'255 to money! 1e-17
)
(
    not negative?
        (to money! 1e-31)
            - abs (to money! -26e-17)
            - round/even/to -$0.000'000'000'000'000'255 to money! 1e-17
)


; While Rebol2 would keep the units of the input as MONEY! if you used a non
; MONEY! value to round to, R3-Alpha seems to have changed this.  #1470
;
(2.6 = round/even/to $2.55 1E-1) ;-- adopts type of rounding unit
($2.6 = round/even/to $2.55 $1E-1) ;-- keeps MONEY!
