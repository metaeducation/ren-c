; functions/control/attempt.r
[#41
    (null? attempt [1 / 0])
]
(1 = attempt [1])

(void? attempt [])
(void? attempt [void void])
(1 = attempt [1 void])

; RETURN stops attempt evaluation
(
    f1: func [return: [integer!]] [attempt [return 1 2] 2]
    1 == f1
)
; THROW stops attempt evaluation
(1 == catch [attempt [throw 1 2] 2])
; BREAK stops attempt evaluation
(null? repeat 1 [attempt [break 2] 2])
; recursion
(1 = attempt [attempt [1]])
(null = attempt [attempt [1 / 0]])

; infinite recursion, no more stack overflow error...
(
    x: 0
    blk: [x: x + 1, if x = 2000 [throw <deep-enough>] attempt blk]
    <deep-enough> = catch [attempt blk]
)

; ATTEMPT is implemented on top of ENTRAP for efficiency, but it should be
; equivalent to the usermode version.
[
    (uattempt: func [
        return: [<opt> any-value!]
        code [block!]
        <local> last'
    ][
        last': void'
        reduce-each ^result' code [
            if raised? unmeta result' [return null]
            if void? unmeta result' [continue]
            last': result'
        ]
        return unmeta last'
    ], true)

    (null? uattempt [1 / 0])
    (30 = uattempt [10 + 20])
    (null? uattempt [uattempt [1 / 0]])
]
