; %parse-here.test.reb
;
; HERE follows Topaz precedent as the new means of capturing positions
; (e.g. POS: HERE).  But it is useful for other purposes, when a rule is
; needed for capturing the current position.

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
