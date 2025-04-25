(
    foo: func [x [integer! <...>]] [
        sum: 0
        while [not tail? x] [
            sum: sum + take x
        ]
    ]
    y: ((z: foo 1 2 3) 4 5)
    all [y = 5  z = 6]
)
(
    foo: func [x [integer! <...>]] [make block! x]
    [1 2 3 4] = foo 1 2 3 4
)

(
    ;-- leaked VARARGS! cannot be accessed after call is over
    error? sys/util/rescue [take reeval (foo: func [x [integer! <...>]] [x])]
)

(
    f: func [args [any-value! ~null~ <...>]] [
       b: take args
       either tail? args [b] ["not at end"]
    ]
    x: make varargs! [_]
    blank? applique :f [args: x]
)

(
    f: func [:look [<...>]] [reify first look]
    '~null~ = applique 'f [look: make varargs! []]
)

; !!! Experimental behavior of infixed variadics, is to act as either 0 or 1
; items.  0 is parallel to <end>, and 1 is parallel to a single parameter.
; It's a little wonky because the evaluation of the parameter happens *before*
; the TAKE is called, but theorized that's still more useful than erroring.
[
    (
        normal: infix function [v [integer! <...>]] [
            sum: 0
            while [not tail? v] [
                sum: sum + take v
            ]
            return sum
        ]
        okay
    )

    (eval [normal] = 0)
    (eval [10 normal] = 10)
    (eval [10 20 normal] = 20)
    (eval [x: 30  y: 'x  1 2 x normal] = 30)
    (eval [multiply 3 9 normal] = 27) ;-- seen as ((multiply 3 9) normal)
][
    (
        tight: infix function [#v [integer! <...>]] [
            sum: 0
            while [not tail? v] [
                sum: sum + take v
            ]
            return sum
        ]
        okay
    )

    (eval [tight] = 0)
    (eval [10 tight] = 10)
    (eval [10 20 tight] = 20)
    (eval [x: 30  y: 'x  1 2 x tight] = 30)
    (eval [multiply 3 9 tight] = 27) ;-- seen as (multiply 3 (9 tight))
][
    (
        soft: infix function ['v [any-value! <...>]] [
            stuff: copy []
            while [not tail? v] [
                append/only stuff take v
            ]
            return stuff
        ]
        okay
    )

    (eval [soft] = [])
    (eval [a soft] = [a])
    (eval [(1 + 2) (3 + 4) soft] = [7])
][
    (
        hard: infix function [:v [any-element! <...>]] [
            stuff: copy []
            while [not tail? v] [
                append/only stuff take v
            ]
            return stuff
        ]
        okay
    )

    (eval [hard] = [])
    (eval [a hard] = [a])
    (eval [(1 + 2) (3 + 4) hard] = [(3 + 4)])
]
