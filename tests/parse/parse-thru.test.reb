; %parse-thru.test.reb
;
; Up to but not including

; Edge case of matching END with THRU
;
(uparse? "" [thru ["a" | <end>]])
(uparse? [] [thru ["a" | <end>]])

[#1959
    (uparse? "abcd" [thru "d"])
    (uparse? "<abcd>" [thru '<abcd>])
    (uparse? [a b c d] [thru 'd])
]

[#1457
    (uparse? "a" compose [thru (charset "a")])
    (not uparse? "a" compose [thru (charset "a") skip])
]

[#2141 (
    xset: charset "x"
    uparse? "x" [thru [xset]]
)]

; THRU advances the input position correctly.
(
    i: 0
    uparse "a." [
        while [thru "a" (i: i + 1 j: try if i > 1 [end skip]) j]
    ]
    i == 1
)
