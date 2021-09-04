; %unpack.test.reb
;
; UNPACK replaces the idiom of SET of a BLOCK! that you REDUCE, in
; order to work around issues related to states that cannot be put
; into blocks (BAD-WORD! isotopes and NULL).  It unifies the REDUCE
; with the operation.

(
    a: b: ~bad~
    did all [
        3 = [a b]: unpack [1 + 2 3 + 4]
        a = 3
        b = 7
    ]
)

(
    a: b: ~bad~
    did all [
        1 = [a b c]: unpack @[1 + 2]
        a = 1
        b = '+
        c = 2
    ]
)

; ... is used to indicate willingness to discard extra values
(
    did all [
        1 = [a b ...]: unpack @[1 2 3 4 5]
        a = 1
        b = 2
    ]
)
