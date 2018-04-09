; functions/control/either.r
(
    either true [success: true] [success: false]
    success
)
(
    either false [success: false] [success: true]
    success
)
(1 = either true [1] [2])
(2 = either false [1] [2])

(void? either* true [] [1])
(void? either* false [1] [])

(blank? either true [] [1])
(blank? either false [1] [])

(error? either true [try [1 / 0]] [])
(error? either false [] [try [1 / 0]])

; RETURN stops the evaluation
(
    f1: does [
        either true [return 1 2] [2]
        2
    ]
    1 = f1
)
(
    f1: does [
        either false [2] [return 1 2]
        2
    ]
    1 = f1
)
; THROW stops the evaluation
(
    1 == catch [
        either true [throw 1 2] [2]
        2
    ]
)
(
    1 == catch [
        either false [2] [throw 1 2]
        2
    ]
)
; BREAK stops the evaluation
(
    blank? loop 1 [
        either true [break 2] [2]
        2
    ]
)
(
    blank? loop 1 [
        either false [2] [break 2]
        2
    ]
)
; recursive behaviour
(2 = either true [either false [1] [2]] [])
(1 = either false [] [either true [1] [2]])
; infinite recursion
(
    blk: [either true blk []]
    error? try blk
)
(
    blk: [either false [] blk]
    error? try blk
)
