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
    void? case [false []] ;-- void indicates no branch was taken
)
(
    void? case [] ;-- empty case block is legal (e.g. as COMPOSE product)
)
(
    blank? case [true []] ;-- blank vs. void, indicates branch was taken
)
(
    blank? case [
        true []
        false [1 + 2]
    ]
)
[#2246 (
    void? case* [true []] ;-- overrides the "blankification"
)]

(
    error? trap [
        case [
            first [a b c] ;-- no corresponding branch, illegal
        ]
    ]
)

(
    flag: false
    did all [
        3 = case [
            true reduce [elide (flag: true) 'add 1 2]
        ]
        flag = true ;-- the REDUCE ran, and BLOCK! was then executed
    ]
)
(
    flag: false
    did all [
        void? case [
            false reduce [elide (flag: true) 'add 1 2]
        ]
        flag = true ;-- the REDUCE ran, but BLOCK! was then ignored
    ]
)

(
    error? trap [
        case [
            true add 1 2 ;-- "branch" is 3, but must be BLOCK! or FUNCTION!
        ]
    ]
)


; RETURN, THROW, BREAK will stop case evaluation
(
    f1: does [case [return 1 2]]
    1 = f1
)
(
    1 = catch [
        case [throw 1 2]
        2
    ]
)
(
    blank? loop 1 [
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
    s1 and (s2)
)]

; nested calls
(1 = case [true [case [true [1]]]])

; infinite recursion
(
    blk: [case blk]
    error? trap blk
)
