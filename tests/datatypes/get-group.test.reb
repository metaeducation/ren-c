; GET-GROUP! tests
;
; Initially `:(x)` was a synonym for `get x`, but this was replaced with the
; idea of doing the same thing as (x) in the evaluator...freeing up shades
; of distinction in dialecting.

(get-group? first [:(a b c)])
(get-tuple? first [:(a b c).d])

(chain! = type of first [:(a b c)])
(chain! = type of first [:(a b c).d])

; !!! What was a GET-GROUP! is now a CHAIN!, and a meaning has not yet been
; decided on--if there will be any--for the regular evaluator.
[
    (comment [
        m: 1020
        word: 'm
        :(word) = the m
    ] ok)

    (comment [
        o: make object! [f: 304]
        tuple: 'o.f
        :(tuple) = the o.f
    ] ok)

    (comment [
        m: 1020
        o: make object! [f: 304]
        block: [m o.f]
        :(block) = [m o.f]
    ] ok)
]

; Groups can pass on errors that happen in their last slot.  Otherwise they
; panic on errors, and you need to use SYS.UTIL/RESCUE.
[
    (
        e: unquasi ^ (1 + 2 fail "handled")
        e.message = "handled"
    )
    (
        e: unquasi meta (1 + 2 fail "handled")
        e.message = "handled"
    )
    (
        e: sys.util/rescue [
            (fail "unhandled" 1 + 2)
        ]
        e.message = "unhandled"
    )
    (
        e: sys.util/rescue [
            unquasi meta (fail "unhandled" 1 + 2)
        ]
        e.message = "unhandled"
    )
]
