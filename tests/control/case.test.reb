; functions/control/case.r
[
    success: false
    case [true [success: true]]
    success
]
[
    success: true
    case [false [success: false]]
    success
]

[
    void? case []
][
    #2246
    void? case* [true []]
][
    '| = case [true []]
]

; case results
[case [true [true]]]
[not case [true [false]]]
; RETURN stops evaluation
[
    f1: does [case [return 1 2]]
    1 = f1
]
; THROW stops evaluation
[
    1 = catch [
        case [throw 1 2]
        2
    ]
]
; BREAK stops evaluation
[
    blank? loop 1 [
        case [break 2]
        2
    ]
]
; /all refinement
; bug#86
[
    s1: false
    s2: false
    case/all [
        true [s1: true]
        true [s2: true]
    ]
    s1 and (s2)
]
; recursivity
[1 = case [true [case [true [1]]]]]
; infinite recursion
[
    blk: [case blk]
    error? try blk
]
