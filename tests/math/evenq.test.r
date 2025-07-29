; functions/math/evenq.r
;
; Narrowed considerably from historical types, should be reviewed:
;
; https://rebol.metaeducation.com/t/odd-defined-in-terms-of-even/2521

(even? 0)
(not even? 1)
(not even? -1)
(not even? 2147483647)
(even? -2147483648)
<64bit>
(not even? 9223372036854775807)
<64bit>
(even? -9223372036854775808)

; time (tests time.seconds)

(even? 0:00)
(even? 0:1:00)
(even? -0:1:00)
(not even? 0:0:01)
(even? 0:0:02)
(not even? -0:0:01)
(even? -0:0:02)

; ODD? is defined as NOT EVEN?

(odd? 1)
(not odd? 2)
