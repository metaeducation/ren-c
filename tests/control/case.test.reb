; %case.test.reb

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
    void? case [false []]
)
(
    void? case []  ; empty case block is legal (e.g. as COMPOSE product)
)
(
    '~[]~ = ^ case [
        true [null]  ; turned to isotope so ELSE won't run
        false [1 + 2]
    ]
)

[#2246 (
    '~[]~ = ^ case [true [null]]  ; indicates branch was taken (vs. null)
)(
    '~()~ = ^ case [true []]
)]

(
    'a = case [
        first [a b c]  ; no corresponding branch, means "case fallout"
    ]
)

(
    3 = case [true (reduce ['add 1 2])]
)
(
    void? case [false (reduce ['add 1 2])]
)

~bad-branch-type~ !! (
    case [
        true add 1 2  ; branch slots must be BLOCK!, ACTION!, softquote
    ]
)

; Invisibles should be legal to mix with CASE.  Being in group or not should
; not affect the behavior.

[(
    flag: false
    result: case [
        1 < 2 [1020]
        elide (flag: true)
        fail "shouldn't get here"
    ]
    (not flag) and (result = 1020)
)(
    flag: false
    result: case [
        1 < 2 [1020]
        elide flag: true
        fail "shouldn't get here"
    ]
    (not flag) and (result = 1020)
)(
    flag: false
    result: case [
        1 < 2 [1020]
        (elide flag: true)
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
    x: 0
    blk: [elide (x: x + 1, if x = 5000 [throw <deep-enough>]) case blk]
    <deep-enough> = catch blk
)


; New feature for specifying predicates

(<a> = case/predicate [1 = 2 [<a>]] :not)
(<b> = case/predicate [1 [<a>] 2 [<b>]] :even?)
(<b> = case/predicate [1 = 1 [<a>]] :not else [<b>])

~bad-branch-type~ !! (case [true #bad])

(1 = case [(void) true [1]])

~bad-isotope~ !! (case [~isotope~ [print "Causes error"]])

; GET-GROUP! branches will be evaluated unconditionally, but their branches
; are not run if the condition was false.
[
    (
        called: false
        did all [
            3 = case [true :(called: true, [1 + 2])]
            called
        ]
    )
    (
        called: false
        did all [
            void? case [false :(called: true, [1 + 2])]
            called
        ]
    )
]
