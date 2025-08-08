; %macro.test.r
;
; MACRO is an exposure of an internal mechanism for splicing raw material into
; the stream of execution, used by predicates.
;
;     >> m: macro [x] [return [append x first]]
;
;     >> m [1 2 3] [d e f]
;     == [1 2 3 d]
;
; While expedient, this does have drawbacks.  For instance: while the
; function appears to take one parameter, in practice it will act as
; if it takes two.  It will not have the same composability with things
; like SPECIALIZE or ADAPT that a formally specified function would.

(
    m: macro [return: [block!] x] [return [append x first]]
    [1 2 3 d] = m [1 2 3] [d e f]
)

; !!! Macros are sketchy in terms of the implementation.  It's possible to do
; them infixedly, but there are a number of sanity checking asserts that are
; blocking it at this time.  The asserts are more valuable than the feature,
; so it's on hold until things can be clarified more
;
;    m: infix macro [discard] [[+ 2]]  ; !!! discard needed ATM
;    1 m = 3


; INLINE is a variant of macro for putting code directly into the feed
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
