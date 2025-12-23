; %maybe.test.r
;
; MAYBE is like the anti-DEFAULT.  It checks the right hand side for
; being void or null, and if it is then it won't overwrite the left.
; But other values will overwrite.

(
    x: 304
    x: maybe null
    x = 304
)

(
    x: 304
    error? x: maybe fail "propagate"
)

(
    x: 304
    x: pack [1000 + 20, <unused>]
    x = 1020
)

(
    x: 304
    x: maybe 1000 + 20
    x = 1020
)

; (x: maybe ^ghost) can't work today, but eventually (^x: maybe ^ghost) should
; be able to work so that if a branching construct produces ^ghost it can
; write to a meta-variable.
;
~???~ !! (
    x: 304
    x: maybe ^ghost
    okay  ; unreachable
)
