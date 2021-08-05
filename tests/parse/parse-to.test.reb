; %parse-to.test.reb

; Edge case of matching END with TO or THRU
;
(uparse? "" [to ["a" | <end>]])
(uparse? [] [to ["a" | <end>]])

; TO and THRU would be too costly to be implicitly value bearing by making
; copies; you need to use ACROSS.
[(
    "" = uparse "abc" [to <end>]
)(
    '~void~ = ^(uparse "abc" [elide to <end>])  ; ornery void
)(
    "b" = uparse "aaabbb" [thru "b" elide to <end>]
)(
    "b" = uparse "aaabbb" [to "b" elide to <end>]
)]

[#1959
    (uparse? "abcd" [to "d" skip])
    (uparse? [a b c d] [to 'd skip])
]

[#1457
    (uparse? "ba" compose [to (charset "a") skip])
    (not uparse? "ba" compose [to (charset "a") "ba"])
]
