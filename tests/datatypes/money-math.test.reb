; %money-math.test.reb
;
; At time of writing, these tests do not work...because the math guts
; behind the MONEY! type were removed.  It's only a string type.
;
; It's possible that extensions could imbue existing types with new
; abilities.  That would be the direction that we'd likely be looking
; at to provide money with something more than just being its string.

; check, whether these are moldable
(
    x: $999999999999999
    any [
        warning? trap [x: x + $1]
        not warning? trap [mold x]
    ]
)
(
    x: $-999999999999999
    any [
        warning? trap [x: x - $1]
        not warning? trap [mold x]
    ]
)
(
    x: ~
    any [
        warning? trap [x: $1234567890123456]
        not warning? trap [mold x]
    ]
)
($111 = make money! 111)
($1.10 = make money! "1.1")
($1 = make money! "1")
($1.10 = make money! "$1.1")

($1 <= $2)
(not ($2 <= $1))
(not zero? $1)
(not zero? $-1)
; positive? tests
(not positive? negate $0)
(positive? $1)
(not positive? $-1)
(not negative? negate $0)
(not negative? $1)
(negative? $-1)
; same? tests
(same? $0 $0)
(same? $0 negate $0)
(same? $1 $1)
(not same? $1 $1.00)
("$1.00" = mold $2.00 - $1)
("$1" = mold $2 - $1)
("$1" = mold $1 * $1)
("$4" = mold $2 * $2)
("$1.00" = mold $1 * $1.00)
("$1.00" = mold $1.00 * $1.00)

[#97
    ($59.00 = (10% * $590))
]
[#97
    ($100.60 = ($100 + 60%))
]

; division uses "full precision"
("$1.00" = mold $1 / $1)
("$1.00" = mold $1 / $1.00)
("$0.10" = mold $1 / $10)
("$0.33" = mold $1 / $3)
("$0.67" = mold $2 / $3)


(-9223372036854775808 = to integer! $-9223372036854775808)
(9223372036854775807 = to integer! $9223372036854775807)

; conversion to decimal
(1.0 = to decimal! $1)
(zero? 0.3 - to decimal! $0.30)
(zero? 0.1 - to decimal! $0.10)
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
    x: to decimal! $1
    zero? x - to decimal! to money! x
)
(
    x: to decimal! $-1
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
    not negative?
        1e-31
            - abs (to money! 26e-17)
            - round:even:to $0.00 to money! 1e-17
)
(
    not negative?
        (to money! 1e-31)
            - abs (to money! -26e-17)
            - round:even:to $-0.00 to money! 1e-17
)


($0 = negate $0)
($-1 = negate $1)
($1 = negate $-1)

(zero? $0)
(not zero? $0.01)
(not zero? $-0.01)
(not zero? $999999999999999.87)
(not zero? $-999999999999999.87)
(zero? negate $0)

; (zero? $999'999'999'999'999 mod 1)
; (zero? $999'999'999'999'999 mod $1)

(not negative? $0)
(not negative? $0.01)
(negative? $-0.01)
(not negative? $999999999999999.87)
(negative? $-999999999999999.87)

(not positive? $0)
(positive? $0.01)
(not positive? $-0.01)
(positive? $999999999999999.87)
(not positive? $-999999999999999.87)

(0 = sign-of $0)
(1 = sign-of $0.01)
(-1 = sign-of $-0.01)

(not odd? $0)
(odd? $1)
(not odd? $2)
(odd? $-1)
(not odd? $-2)
(odd? $999999999999999)
(odd? $-999999999999999)

(even? $0)
(not even? $1)
(even? $2)
(not even? $-1)
(even? $-2)
(not even? $999999999999999)
(not even? $-999999999999999)

(equal? 0 $0)
(equal? 0.0 $0)
(equal? $0 0%)
(equal? equal? 1 $1 equal? $1 1)
(equal? equal? 1.0 $1 equal? $1 1.0)
(equal? equal? $1 100% equal? 100% $1)

(not equal? 0 $0)
(not equal? 0 0%)
(not equal? 0.0 $0)
(not equal? $0 0%)
(equal? equal? 1 $1 equal? $1 1)
(equal? equal? 1.0 $1 equal? $1 1.0)
(equal? equal? $1 100% equal? 100% $1)

(not same? 0 $0)
(not same? 0.0 $0)
(not same? $0 0%)
(equal? same? 1 $1 same? $1 1)
(equal? same? 1.0 $1 same? $1 1.0)
(equal? same? $1 100% same? 100% $1)

($101 = round $100.50)
($-101 = round $-100.50)

; REBOL2 rounds to $100.50 beyond this
($100 = round $100.49)
; REBOL2 rounds to $100.50 beyond this
($-100 = round $-100.49)
; REBOL2 rounds to $1000.5 beyond this
($1000 = round $1000.49)
; REBOL2 rounds to $1000.5 beyond this
($-1000 = round $-1000.49)

($100 = round:even $100.25)
($-100 = round:even $-100.25)

; While Rebol2 would keep the units of the input as MONEY! if you used a non
; MONEY! value to round to, R3-Alpha seems to have changed this.  #1470
;
(2.6 = round:even:to $2.55 1E-1)  ; adopts kind of rounding unit
($2.60 = round:even:to $2.55 $0.10)  ; keeps MONEY!

[#1116
    ($1.15 = round:even:to 1.15 $0.01)
]
(1.15 = round:even:to $1.15 0.01)

(zero? $100.20 - round:to 100.15 $0.10)
($100.20 = round:to $100.15 $0.10)
($100 = round:to $100.15 $2)
[#1470
    (2.6 = round:even:to $2.55 0.10)
]
[#1470
    ($2.60 = round:even:to 2.55 $0.10)
]
($-2.60 = round:even:to $-2.55 $0.10)
($0.00 = ($0.00 - round:even:to $0.00 $0.01))
($2.60 = round:even:to $2.55 $0.10)

($100 = round:half-ceiling $100)
($101 = round:half-ceiling $100.50)
($101 = round:half-ceiling $100.51)
($-100 = round:half-ceiling $-100)
($-100 = round:half-ceiling $-100.50)
[#1471
    ($-101 = round:half-ceiling $-100.51)
]

($100 = round:half-down $100)
($100 = round:half-down $100.50)
($101 = round:half-down $100.51)
($-100 = round:half-down $-100)
($-100 = round:half-down $-100.50)
($-101 = round:half-down $-100.51)

(zero? modulo:adjusted $0.10 + $0.10 + $0.10 $0.30)
(zero? modulo:adjusted $0.30 $0.10 + $0.10 + $0.10)
