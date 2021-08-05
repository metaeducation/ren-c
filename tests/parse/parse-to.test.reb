; %parse-to.test.reb

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
