; %parse-here.test.reb
;
; As alternatives to using SET-WORD! to set the uparse position and GET-WORD!
; to get the uparse position, Ren-C has <here> and the SEEK keyword.  HERE
; follows Topaz precedent as the new means of capturing positions
; (e.g. POS: HERE).  But it is useful for other purposes, when a rule is
; needed for capturing the current position.
;
; https://github.com/giesse/red-topaz-uparse

[(
    did all [
        uparse? "aaabbb" [some "a", pos: <here>, some "b"]
        pos = "bbb"
    ]
)(
    did all [
        uparse? "<<<stuff>>>" [
            left: across some "<"
            (n: length of left)
            x: between <here> repeat (n) ">"
        ]
        x = "stuff"
    ]
)]

(
    res: uparse? ser: [x y] [pos: <here>, skip, skip]
    all [res, pos = ser]
)
(
    res: uparse? ser: [x y] [skip, pos: <here>, skip]
    all [res, pos = next ser]
)
(
    res: uparse? ser: [x y] [skip, skip, pos: <here>]
    all [res, pos = tail of ser]
)
[#2130 (
    res: uparse? ser: [x] [pos: <here>, val: word!]
    all [res, val = 'x, pos = ser]
)(
    res: uparse? ser: "foo" [pos: <here>, val: across <any>]
    all [not res, val = "f", pos = ser]
)]

; Should return the same series type as input (Rebol2 did not do this)
; PATH! cannot be PARSE'd due to restrictions of the implementation
(
    a-value: first [a/b]
    uparse as block! a-value [b-value: here]
    a-value = to path! b-value
)
(
    a-value: first [()]
    uparse a-value [b-value: here]
    same? a-value b-value
)
