; functions/control/either.r
(
    success: either okay ['yes] ['no]
    yes? success
)
(
    success: either null ['no] ['yes]
    yes? success
)
(1 = either okay [1] [2])
(2 = either null [1] [2])

('~[~null~]~ = lift either okay [null] [1])
('~[~null~]~ = lift either null [1] [null])

(warning? either okay [rescue [1 / 0]] [])
(warning? either null [] [rescue [1 / 0]])

; RETURN stops the evaluation
(
    f1: func [return: [integer!]] [
        either okay [return 1 2] [2]
        2
    ]
    1 = f1
)
(
    f1: func [return: [integer!]] [
        either null [2] [return 1 2]
        2
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

[
    ; Recursive behavior

    (2 = either okay [either null [1] [2]] [])
    (1 = either null [] [either okay [1] [2]])

    ; Infinite recursion

    (<deep-enough> = catch wrap [
        depth: 0
        eval blk: [
            depth: me + 1
            if depth = 1000 [throw <deep-enough>]
            either okay (blk) []
        ]
    ])
    (<deep-enough> = catch wrap [
        depth: 0
        eval blk: [
            depth: me + 1
            if depth = 1000 [throw <deep-enough>]
            either null [] (blk)
        ]
    ])
]

[
    ; This exercises "deferred typechecking"; even though it passes through a
    ; step where there is a void in the condition slot, that's not the final
    ; situation since the equality operation will be run later, so the test
    ; has to wait.

    (
        takes-2-logics: func [x [logic?] y [logic?]] [return x]
        infix-voider: infix func [return: [~word?~] x y] [
            return '~bad~
        ]
        ok
    )

    (takes-2-logics ('~bad~) = '~bad~ null)

    ~expect-arg~ !! (takes-2-logics okay infix-voider okay null)
]

; Soft Quoted Branching
; https://forum.rebol.info/t/soft-quoted-branching-light-elegant-fast/1020
(
    [1 + 2] = either okay '[1 + 2] [3 + 4]
)(
    7 = either null '[1 + 2] [3 + 4]
)(
    1020 = either okay '1020 '304
)

; @XXX Branching (TBD)
;(
;    j: 304
;    304 = either okay @j [panic "Shouldn't run"]
;)(
;    o: make object! [b: 1020]
;    1020 = either okay @o/b [panic "Shouldn't run"]
;)(
;    var: <something>
;    all [
;        304 = either null @(var: <something-else> [1000 + 20]) [300 + 4]
;        var = <something>
;        1020 = if ok @(var: <something-else> [1000 + 20]) [300 + 4]
;        var = <something-else>
;    ]
;)
