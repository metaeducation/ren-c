; functions/context/valueq.r

; !!! We could answer false to SET? 'nonsense, if it is merely attached but
; has no definition in the attached context or anything it inherits from.
; But being conservative about this is probably the best way to start moving
; toward something closer to a JavaScript "strict mode" type of operation.
; An error is raised for now.
;
(did all [
    e: trap [set? 'utternonsense]
    e.id = 'unassigned-attach
    e.arg1 = 'utternonsense
])

(true == set? 'set?)

[#1914 (
    set? reeval lambda [x] ['x] blank
)(
    set? reeval func [x] [return 'x] blank
)]
