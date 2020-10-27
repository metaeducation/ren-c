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

(void? either true [null] [1])
(void? either false [1] [null])

(null? either true @[null] [1])
(null? either false [1] @[null])

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
    null? loop 1 [
        either true [break 2] [2]
        2
    ]
)
(
    null? loop 1 [
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

[
    ; This exercises "deferred typechecking"; even though it passes through a
    ; step where there is a void in the condition slot, that's not the final
    ; situation since the equality operation will be run later, so the test
    ; has to wait.

    (
        takes-2-logics: func [x [logic!] y [logic!]] [x]
        infix-voider: enfixed func [return: <void> x y] []
        true
    )

    (takes-2-logics (void) = void false)

    ('expect-arg = (trap [takes-2-logics true infix-voider true false])/id)
]

; Soft Quoted Branching
; https://forum.rebol.info/t/soft-quoted-branching-light-elegant-fast/1020
(
    [1 + 2] = either true '[1 + 2] [3 + 4]
)(
    7 = either false '[1 + 2] [3 + 4]
)(
    1020 = either true '1020 '304
)

; Lit-Branching
(
    j: 304
    304 = either true @j [fail "Shouldn't run"]
)(
    o: make object! [b: 1020]
    1020 = either true @o/b [fail "Shouldn't run"]
)(
    var: <something>
    did all [
        304 = either false @(var: <something-else> [1000 + 20]) [300 + 4]
        var = <something>
        1020 = if true @(var: <something-else> [1000 + 20]) [300 + 4]
        var = <something-else>
    ]
)
