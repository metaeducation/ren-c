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
(error? sys/util/rescue [make blank! [a b c]])
(null = make integer! void)
(null = make object! void)

(null? to blank! void)  ;-- TO's universal protocol for void 2nd argument
(null? to void 1) ;-- TO's universal protocol for void 1st argument
(error? sys/util/rescue [to blank! 1]) ;-- no other types allow "conversion" to blank

("_" = mold blank)
[#1666 #1650 (
    f: does [_]
    _ == f
)]

[
    (void? for-each x _ [1020])
    ([] = map-each x _ [1020])
    (void? for-next x _ [1020])
    (0 = remove-each x _ [okay])
    (void? every x _ [okay])
    (void? for-skip x _ 2 [1020])

    (null = take _)
    (null = find _ 304)
    (null = select _ 304)
    (null = pick _ 304)

    (_ = copy _)  ; do NOT want opt out behavior for copy...!
]
