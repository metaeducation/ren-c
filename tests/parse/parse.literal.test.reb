; %parse-literal.test.reb
;
; See %parse-just.test.reb for an explanation of the two forms of literal
; rule usage:
;
;     >> parse [] [just a]  ; just synthesize (don't match)
;     == a
;
;     >> parse [''a] [literal ''a]  ; match literally (clearer than '''a)
;     == ''a

[
    ('wb = parse [wb] [literal wb])
    (123 = parse [123] [lit 123])
    (3 = parse [3 3] [repeat 2 literal 3])
    ('_ = parse [_] [lit _])
    ('some = parse [some] [literal some])
]

[#1314 (
    d: [a b c 1 d]
    ok
)(
    'd = parse d [thru lit 1 'd]
)(
    1 = parse d [thru 'c literal 1 elide 'd]
)]
