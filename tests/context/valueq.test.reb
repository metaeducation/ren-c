; functions/context/valueq.r

; !!! We could answer false to SET? $nonsense, if it is merely attached but
; has no definition in the attached context or anything it inherits from.
; But being conservative about this is probably the best way to start moving
; toward something closer to a JavaScript "strict mode" type of operation.
; It panics for now.
;
(all [
    let e: sys.util/rescue [set? $utternonsense]
    e.id = 'not-bound
    e.arg1 = 'utternonsense
])

(set? $set?)

[#1914 (
    set? run lambda [x] [$x] space
)(
    set? run func [x] [return $x] space
)]
