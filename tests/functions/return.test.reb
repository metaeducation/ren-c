; functions/control/return.r

(
    f1: func [return: [integer!]] [return 1 2]
    1 = f1
)
(
    success: 'true
    f1: func [return: [integer!]] [return 1 success: 'false]
    f1
    true? success
)

; return value tests
(
    f1: func [return: [any-value?]] [return null]
    null? f1
)
(
    f1: func [return: [warning!]] [return trap [1 / 0]]
    warning? f1
)

[#1515 ; the "result" of return should not be assignable
    (a: 1 run func [return: [integer!]] [a: return 2] a = 1)
]

(a: 1 reeval reify func [return: [integer!]] [set $a return 2] a = 1)
(a: 1 run func [return: [integer!]] [set:any $a return 2] a = 1)

[#1509 ; the "result" of return should not be passable to functions
    (a: 1 reeval reify func [return: [integer!]] [a: warning? return 2] a = 1)
]

[#1535
    (run func [return: [integer!]] [words of return 1020] ok)
]

(reeval reify func [return: [integer!]] [values of return 1020] ok)

[#1945
    (run func [return: [integer!]] [spec-of return 1020] ok)
]

; return should not be caught by TRAP
(
    a: 1 reeval reify func [return: [integer!]] [a: warning? trap [return 2]]
    a = 1
)

(
    success: 'true
    f1: func [return: []] [return ~, success: 'false]
    f1
    true? success
)
(
    f1: func [return: []] [return ~]
    (meta trash) = meta f1
)
[#1515 (  ; the "result" of a return should not be assignable
    a: 1
    run func [return: []] [a: return ~]
    a = 1
)]
(a: 1 reeval reify func [return: []] [set $a return ~] a = 1)
(a: 1 reeval reify func [return: []] [set:any $a return ~] a = 1)
[#1509 (  ; the "result" of a return should not be passable to functions
    a: 1
    run func [return: []] [a: warning? return ~]
    a = 1
)]
[#1535
    (trash? reeval noquasi reify func [return: []] [words of return ~])
]
(trash? run func [return: []] [values of return ~])
[#1945
    (trash? run func [return: []] [spec-of return ~])
]


; === TAIL CALLS ===

; RETURN:RUN with current values of frame arguments
(
    foo: func [return: [tag!] n <local> clear-me] [
        assert [unset? $clear-me]
        if n = 0 [
            return <success>
        ]
        n: n - 1
        clear-me: #some-junk
        return:run <redo>
    ]

    <success> = foo 100
)

; RETURN:RUN with a new call (doesn't reuse arg cells, because it needs the
; old values while calculating the new ones)
(
    foo: func [return: [tag!] n <local> clear-me] [
        assert [unset? $clear-me]
        if n = 0 [
           return <success>
        ]
        clear-me: #some-junk
        return:run foo/ n - 1
    ]

    <success> = foo 100
)

; RETURN:RUN can call any function, not just the one you're returning from
; (But the savings are less, as it's only reusing the Level structure)
(
    foo: func [return: [tag!] block] [
        return:run append/ block [d e]
    ]

    [a b c [d e]] = foo [a b c]
)

; REDO type checking test
; (args and refinements must pass function's type checking)
;
~expect-arg~ !! (
    foo: func [return: [tag!] n i [integer!]] [
        if n = 0 [
            return <success>  ; impossible for this case
        ]
        n: n - 1
        i: #some-junk  ; type check should panic on redo
        return:run <redo>
    ]

    foo 100 1020
)

; RETURN:RUN <REDO> phase test
; (shared frame compositions should redo the appropriate "phase")
(
    inner: func [return: [tag!] n] [
        if n = 0 [
            return <success>
        ]
        n: 0
        return:run <redo>  ; should redo INNER, not outer
    ]

    /outer: adapt inner/ [
        if n = 0 [
            panic "inner phase should have been run by redo"
        ]
        ; fall through to inner, using same frame
    ]

    <success> = outer 1
)
