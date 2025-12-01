; %macro.test.r
;
; INLINER is an exposure of an internal mechanism for splicing raw material
; into the stream of execution.
;
; While expedient, it's fundamentally macro-like and so it may be variadic.
; It will not have the same composability with things like SPECIALIZE or ADAPT
; that a formally specified function would.

(
    m: inliner [x] [spread compose [append (x) first]]
    [1 2 3 d] = m [1 2 3] [d e f]
)

(
    m: infix inliner [left] [spread compose [(left) + 2]]
    1 m = 3
)


; INLINE is a variant of inliners for putting code directly into the feed
; It accepts BLOCK! (spread contents), QUOTED! (splice single value) or just
; VOID as a no-op.
[
    (
        y: ~
        x: 1 + inline [2 y: negate] 10
        all [
            x = 3
            y = -10
        ]
    )
    (0 = (1 + inline the 'negate 1))
    (3 = (1 + inline ^void 2))
]
