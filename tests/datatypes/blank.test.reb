; datatypes/none.r
(blank? blank)
(not blank? 1)
(blank! = type of blank)
; literal form
(null? _)
[#845
    (blank = '_)
]

(null = make blank! maybe null)
(error? trap [make blank! [a b c]])

(null? to blank! maybe null)  ; TO's universal protocol for void 2nd argument
(null? to maybe null 1)  ; TO's universal protocol for blank 1st argument

~???~ !! (to blank! 1)  ; no other types allow "conversion" to blank

("_" = mold blank)
[#1666 #1650 (
    f: does [_]
    null = f
)]
