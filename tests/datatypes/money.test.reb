; datatypes/money.r
;
; Ren-C removed the "Deci" datatype which underlied MONEY!.  It turned it into
; just a type of string.
;
; The reason was that the deci datatype had no maintainer and was very complex
; code, beyond the main concerns of the Ren-C project.  The primitive string
; representation keeps money being LOAD-able, and provides a stable ability
; to represent its most important forms... e.g. $1 $2 $3 which are useful
; especially in dialects for non-money-representing purposes.

(money? $0.00)
(not money? 0)
(money! = type of $0.00)
(money? $1.00)
(money? $-1.00)
(money? $1.50)

[#4
    ($111 = to money! 111)
]
($1 = to money! "1")
~???~ !! (to money! "1.1")
("$1.10" = mold $1.10)
("$-1.10" = mold $-1.10)
("$0" = mold $0)
; equality
($1 = $1.00)
(not ($1 = $2))
; inequality
(not ($1 <> $1))

; conversion to integer
(1 = to integer! $1)

~???~ !! (to integer! $9223372036854775808.99)  ; can't TO with decimal point

($1 = reeval $1)
($1 = eval [$1])

(
    f: does [$1]
    $1 == f
)

(if $1 [okay])

($1 == any [$1])
($1 == any [null $1])
($1 == any [$1 null])

($1 == all [$1])
($1 == all [okay $1])
(okay = all [$1 okay])

; moldable maximum for R2
(money? $999999999999999.87)
; moldable minimum for R2
(money? $-999999999999999.87)
