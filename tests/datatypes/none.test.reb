; datatypes/none.r
(blank? blank)
(not blank? 1)
(blank! = type of blank)
; literal form
(blank = _)
[#845
    (blank = _)
]

; MAKE of a BLANK! is not legal, but MAKE TARGET-TYPE VOID follows rule of
; void-in null-out
;
(null = make blank! void)
(error? trap [make blank! [a b c]])
(null = make integer! void)
(null = make object! void)

(null? to blank! void)  ;-- TO's universal protocol for void 2nd argument
(null? to void 1) ;-- TO's universal protocol for void 1st argument
(error? trap [to blank! 1]) ;-- no other types allow "conversion" to blank

("_" = mold blank)
[#1666 #1650 (
    f: does [_]
    _ == f
)]
