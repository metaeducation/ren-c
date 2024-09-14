; %case.test.reb

(okay = case [okay [okay]])
(null = case [okay [null]])
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
    null? case [null []]
)
(
    null? case []  ; maybe useful (e.g. as COMPOSE product) !!! make it void?
)
(
    '~[~null~]~ = ^ case [
        okay [null]  ; turned to heavy null pack so ELSE won't run
        null [1 + 2]
    ]
)

[#2246 (
    '~[~null~]~ = ^ case [okay [null]]  ; indicates branch was taken (vs. null)
)(
    '~[~void~]~ = ^ case [okay []]
)]

(
    'a = case [
        first [a b c]  ; no corresponding branch, means "case fallout"
    ]
)

(
    3 = case [okay (reduce ['add 1 2])]
)
(
    null? case [null (reduce ['add 1 2])]
)

~bad-branch-type~ !! (
    case [
        okay add 1 2  ; branch slots must be BLOCK!, ACTION!, softquote
    ]
)

; Invisibles should be legal to mix with CASE.  Being in group or not should
; not affect the behavior.

[(
    flag: null
    result: case [
        1 < 2 [1020]
        elide (flag: okay)
        fail "shouldn't get here"
    ]
    (not flag) and (result = 1020)
)(
    flag: null
    result: case [
        1 < 2 [1020]
        elide flag: okay
        fail "shouldn't get here"
    ]
    (not flag) and (result = 1020)
)(
    flag: null
    result: case [
        1 < 2 [1020]
        (elide flag: okay)
        fail "shouldn't get here"
    ]
    (not flag) and (result = 1020)
)]




; RETURN, THROW, BREAK will stop case evaluation
(
    f1: func [return: [integer!]] [case [return 1 2]]
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
    s1 and (s2)
)]

; nested calls
(1 = case [okay [case [okay [1]]]])

; infinite recursion
(
    <deep-enough> = catch [
        x: 0
        blk: [elide (x: x + 1, if x = 5000 [throw <deep-enough>]) case blk]
        eval blk
    ]
)


; New feature for specifying predicates

(<a> = case/predicate [1 = 2 [<a>]] :not)
(<b> = case/predicate [1 [<a>] 2 [<b>]] :even?)
(<b> = case/predicate [1 = 1 [<a>]] :not else [<b>])

~bad-branch-type~ !! (case [okay #bad])

(1 = case [(comment "hi") okay [1]])

~bad-void~ !! (case [(void) okay [1]])

~bad-antiform~ !! (case [~antiform~ [print "Causes error"]])

; GET-GROUP! branches will be evaluated unconditionally, but their branches
; are not run if the condition was null.
[
    (
        called: null
        all [
            3 = case [okay :(called: okay, [1 + 2])]
            called
        ]
    )
    (
        called: null
        all [
            null? case [null :(called: okay, [1 + 2])]
            called
        ]
    )
]

([a] = case [okay '[a]])
