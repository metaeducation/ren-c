; functions/control/unless.r
(
    success: false
    unless false [success: true]
    success
)
(
    success: true
    unless true [success: false]
    success
)
(1 = unless false [1])

(void? unless* true [1])
(void? unless* false [])
(void? unless true [1])
(blank? unless false [])

(error? unless false [try [1 / 0]])

; RETURN stops the evaluation
(
    f1: does [
        unless false [return 1 2]
        2
    ]
    1 = f1
)
