; GET-GROUP! tests
;
; Initially `:(x)` was a synonym for `get x`, but this was replaced with the
; idea of doing the same thing as (x) in the evaluator...freeing up shades
; of distinction in dialecting.

(get-group! = type of first [:(a b c)])
(get-path! = type of first [:(a b c)/d])

(
    m: 1020
    word: 'm
    :(word) = lit 'm
)

(
    o: make object! [f: 304]
    path: 'o/f
    :(path) = lit 'o/f
)

(
    m: 1020
    o: make object! [f: 304]
    block: [m o/f]
    :(block) = [m o/f]
)
