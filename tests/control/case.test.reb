; functions/control/case.r

(okay = case [okay [okay]])
(trashified? case [okay [null]])
(
    success: null
    case [okay [success: okay]]
    success
)
(
    success: okay
    case [null [success: null]]
    success
)

(
    null? case [null []] ;-- null indicates no branch was taken
)
(
    null? case [] ;-- empty case block is legal (e.g. as COMPOSE product)
)
(
    trash? case [okay []]  ;-- trash indicates branch was taken (vs. null)
)
(
    trash? case [
        okay []
        null [1 + 2]
    ]
)
[#2246 (
    trash? case [okay []]
)]

(
    'a = case [
        first [a b c] ;-- no corresponding branch, means "case fallout"
    ]
)

(
    3 = case [okay (reduce ['add 1 2])]
)
(
    null? case [null (reduce ['add 1 2])]
)

(
    error? sys/util/rescue [
        case [
            okay add 1 2 ;-- branch slots must be BLOCK!, ACTION!, softquote
        ]
    ]
)

; Invisibles should be legal to mix with CASE.

(
    flag: null
    result: case [
        1 < 2 [1020]
        (flag: okay, null) [fail "shouldn't get here"]  ; poor man's elide
        okay [fail "shouldn't get here"]
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
    s1: null
    s2: null
    case/all [
        okay [s1: okay]
        okay [s2: okay]
    ]
    s1 and [s2]
)]

; nested calls
(1 = case [okay [case [okay [1]]]])

; infinite recursion
(
    blk: [case blk]
    error? sys/util/rescue blk
)
