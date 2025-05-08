; functions/control/either.r
(
    either okay [success: okay] [success: null]
    success
)
(
    either null [success: null] [success: okay]
    success
)
(1 = either okay [1] [2])
(2 = either null [1] [2])

(null? either okay [null] [1])
(null? either null [1] [null])

(error? either okay [sys/util/rescue [1 / 0]] [])
(error? either null [] [sys/util/rescue [1 / 0]])

; RETURN stops the evaluation
(
    f1: func [] [
        either okay [return 1 2] [2]
        return 2
    ]
    1 = f1
)
(
    f1: func [] [
        either null [2] [return 1 2]
        return 2
    ]
    1 = f1
)
; THROW stops the evaluation
(
    1 = catch [
        either okay [throw 1 2] [2]
        2
    ]
)
(
    1 = catch [
        either null [2] [throw 1 2]
        2
    ]
)
; BREAK stops the evaluation
(
    null? repeat 1 [
        either okay [break 2] [2]
        2
    ]
)
(
    null? repeat 1 [
        either null [2] [break 2]
        2
    ]
)
; recursive behaviour
(2 = either okay [either null [1] [2]] [])
(1 = either null [] [either okay [1] [2]])
; infinite recursion
(
    blk: [either okay blk []]
    error? sys/util/rescue blk
)
(
    blk: [either null [] blk]
    error? sys/util/rescue blk
)
