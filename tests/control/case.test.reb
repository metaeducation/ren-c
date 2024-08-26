; functions/control/case.r

(true = case [true [true]])
(false = case [true [false]])
(
    success: false
    case [true [success: true]]
    success
)
(
    success: true
    case [false [success: false]]
    success
)

(
    null? case [false []] ;-- null indicates no branch was taken
)
(
    null? case [] ;-- empty case block is legal (e.g. as COMPOSE product)
)
(
    nothing? case [true []]  ;-- trash indicates branch was taken (vs. null)
)
(
    nothing? case [
        true []
        false [1 + 2]
    ]
)
[#2246 (
    nothing? case [true []]
)]

(
    'a = case [
        first [a b c] ;-- no corresponding branch, means "case fallout"
    ]
)

(
    3 = case [true (reduce ['add 1 2])]
)
(
    null? case [false (reduce ['add 1 2])]
)

(
    error? trap [
        case [
            true add 1 2 ;-- branch slots must be BLOCK!, ACTION!, softquote
        ]
    ]
)

; Invisibles should be legal to mix with CASE.

(
    flag: false
    result: case [
        1 < 2 [1020]
        elide (flag: true)
        true [fail "shouldn't get here"]
    ]
    (not flag) and [result = 1020]
)



; RETURN, THROW, BREAK will stop case evaluation
(
    f1: func [] [case [return 1 2]]
    1 = f1
)
(
    1 = catch [
        case [throw 1 2]
        2
    ]
)
(
    null? repeat 1 [
        case [break 2]
        2
    ]
)

[#86 (
    s1: false
    s2: false
    case/all [
        true [s1: true]
        true [s2: true]
    ]
    s1 and [s2]
)]

; nested calls
(1 = case [true [case [true [1]]]])

; infinite recursion
(
    blk: [case blk]
    error? trap blk
)
