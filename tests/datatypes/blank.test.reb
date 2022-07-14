; datatypes/none.r
(blank? blank)
(not blank? 1)
(blank! = type of blank)
; literal form
(blank = _)
[#845
    (blank = _)
]

(null = try make blank! null)
(error? trap [make blank! [a b c]])

(null? try to blank! null)  ; TO's universal protocol for blank 2nd argument
(null? try to null 1)  ; TO's universal protocol for blank 1st argument
(error? trap [to blank! 1])  ; no other types allow "conversion" to blank

("_" = mold blank)
[#1666 #1650 (
    f: does [_]
    _ == f
)]
