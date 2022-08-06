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
    :(word) = the m
)

(
    o: make object! [f: 304]
    tuple: 'o.f
    :(tuple) = the o.f
)

(
    m: 1020
    o: make object! [f: 304]
    block: [m o.f]
    :(block) = [m o.f]
)

; Groups can pass on a failure that happens as their last slot.  Otherwise
; you need to use something like ATTEMPT.
[
    (
        e: unquasi ^ (1 + 2 fail "handled")
        e.message = "handled"
    )
    (
        e: unquasi ^(1 + 2 fail "handled")
        e.message = "handled"
    )
    (
        e: trap [
            (fail "unhandled" 1 + 2)
        ]
        e.message = "unhandled"
    )
    (
        e: trap [
            unquasi ^(fail "unhandled" 1 + 2)
        ]
        e.message = "unhandled"
    )
]
