; datatypes/none.r
(blank? blank)
(not blank? 1)
(blank! == type of blank)
; literal form
(blank == _)
[#845
    (blank == _)
]

; MAKE of a BLANK! is not legal, and MAKE TARGET-TYPE BLANK follows the rule
; of "blanks in, voids out"
;
(error? trap [make blank! blank])
(error? trap [make blank! [a b c]])
(null == make integer! blank)
(null == make object! blank)

; TO cannot follow the "blank in, null out" rule, as it is a "full spectrum"
; operation (like SELECT).
;
; `append value-of-type-x value-of-type-y` is defined as being equivalent
; to `append value-of-type-x (to type-y value-of-type-y)`, hence TO BLOCK!
; of a BLANK! must be `[_]`.
;
; The idea of making TO BLANK! of any type come back as BLANK! may not seem
; too useful, but it might be, if writing `to target-type x` and one wants
; to use `target-type: blank!` as a way of opting out.  Review.
;
(blank? to blank! blank)
(blank == to blank! 1)

("_" == mold blank)
[#1666 #1650 (
    f: does [_]
    _ == f
)]
