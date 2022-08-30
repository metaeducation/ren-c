; MULTIPLE RETURNS TESTS
;
; Multiple returns in Ren-C are done in a way very much in like historical
; Rebol, where WORD! or PATH! gets passed in to a routine which is the
; variable that is then set by the code.
;
; The difference is that it offers the ability to specifically label such
; refinements as outputs, and if this is done then it will participate
; in the evaluator with the SET-BLOCK! construct, which will do an ordered
; injection of parameters from the left-hand side.  It takes care of
; pre-composing any PATH!s with GROUP!s in them, as well as voiding the
; variables.
;
; This results in functions that can be used in the traditional way or
; that can take advantage of the shorthand.

[
    (test: func [x @y @z] [
        if wanted? 'y [
            y: <y-result>
        ]
        if wanted? 'z [
            z: <z-result>
        ]

        return 304
    ]
    true)

    (304 = test 1020)

    (did all [
        304 = [a]: test 1020
        a = 304
    ])

    (did all [
        304 = [b c]: test 1020
        b = 304
        c = <y-result>
    ])

    (did all [
        304 = [d e f]: test 1020
        d = 304
        e = <y-result>
        f = <z-result>
    ])

    (did all [
        304 = [g _ h]: test 1020
        g = 304
        h = <z-result>
    ])

    ; "Circling" results using THE-XXX! is a way of making the overall
    ; multi-return result of the expression come from another output.

    (
        a: b: c: _
        did all [
            <y-result> = [a @b c]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: _
        did all [
            304 = [@a b c]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: _
        did all [
            <z-result> = [a b @(first [c])]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: _
        did all [
            <z-result> = [a b @(#)]: test 1020
            a = 304
            b = <y-result>
            c = null
        ]
    )
]

[
    (
        foo: func [return: [integer!] @other [integer!] arg] [
            other: 10
            return 20
        ]
        null? until [[# #]: foo break]
    )
]

; Opting out of a primary return result can't (currently) be detected by the
; callee, but it will give a none isotope back instead of the actual result.
;
; !!! Should this be void instead?  This would require not writing into the
; output cell directly to erase the result--which adds overhead by taking away
; the evaluator SPARE cell due to writing it as scratch, which we don't want to
; do in the general case.  Letting people ELIDE the NONE! lets those who want
; to pay for it get the same effect.  Additionally, having it be something
; that generates an error allows us to change or mind later...since people
; will likely not depend on it and elide it, which would be harmless if it
; ultimately did become void.
[(
    did all [
        none? [_ rest]: transcode "abc def"
        rest = " def"
    ]
)(
    did all [
        none? [(_) rest]: transcode "abc def"
        rest = " def"
    ]
)(
    raised? [_]: raise "a"
)]

; The META-XXX! types can be used to ask for variables to be raised to a meta
; level, and can be used also as plain ^
[
    (
        a: b: ~
        did all [
            (the 'A) = [^a ^b]: transcode "A B"
            a = the 'A
            b = the '{ B}
        ]
    )(
        a: b: ~
        did all [
            (the 'A) = [^ ^b]: transcode "A B"
            ^a = '~
            b = the '{ B}
        ]
    )
]

; Enfix processing when using a multi-return should match the processing when
; you are using an ordinary return.
[
    (
        value: rest: ~
        did all [
            <item!> = [value rest]: transcode "ab cd" then [<item!>]
            value = <item!>
            rest = " cd"
        ]
    )
    (
        value: rest: ~
        did all [
            <item!> = ([value rest]: transcode "ab cd") then [<item!>]
            value = 'ab
            rest = " cd"
        ]
    )

    (
        foo: func [return: [integer!] @other [integer!]] [
            if wanted? 'other [other: 10] return 20
        ]
        did all [
            '~weird~ = [^x y]: foo then [~weird~]
            x = '~weird~
            y = 10
        ]
    )

    (
        did all [
            x: find "abc" 'b then [10]
            x = 10
            [y]: find "abc" 'b then [10]
            y = 10
        ]
    )
]

; You can use a @ without a variable to get a return result
;
(did all [
    " cd" = [item @]: transcode "ab cd"
    item = 'ab
])

; Propagates void signals, but sets variables to null
[
    (did all [
        void? [x]: comment "hi"
        unset? 'x
    ])
]
