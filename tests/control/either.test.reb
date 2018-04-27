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

(error? either true [trap [1 / 0]] [])
(error? either false [] [trap [1 / 0]])

; RETURN stops the evaluation
(
    f1: func [] [
        either true [return 1 2] [2]
        2
    ]
    1 = f1
)
(
    f1: func [] [
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
    error? trap blk
)
(
    blk: [either false [] blk]
    error? trap blk
)
(
    ; This exercises "deferred typechecking"; even though it passes through a
    ; step where there is a void in the condition slot, that's not the final
    ; situation since the equality operation will be run later, so the test
    ; has to wait.
    ;
    either () = () [true] [false]
)
(
    ; complement to the above, need to type check the final product
    ;
    infix-voider: enfix func [return: [<opt>] x y] []
    'no-value = (trap [either 1 infix-voider 2 [false] [false]])/id
)
