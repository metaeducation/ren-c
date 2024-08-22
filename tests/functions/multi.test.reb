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
        y: <y-result>
        z: <z-result>

        return 304
    ]
    true)

    (304 = test 1020)

    (all [
        304 = [a]: test 1020
        a = 304
    ])

    (all [
        304 = [b c]: test 1020
        b = 304
        c = <y-result>
    ])

    (all [
        304 = [d e f]: test 1020
        d = 304
        e = <y-result>
        f = <z-result>
    ])

    (all [
        304 = [g _ h]: test 1020
        g = 304
        h = <z-result>
    ])

    ; "Circling" results using THE-XXX! is a way of making the overall
    ; multi-return result of the expression come from another output.

    (
        a: b: c: null
        all [
            <y-result> = [a @b c]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        all [
            304 = [@a b c]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        all [
            <z-result> = [a b @(inside [] first [c])]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        all [
            <z-result> = [a b @(void)]: test 1020
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
        null? until [[@ _]: foo break]
    )
]

[(
    all [
        'abc = [_ rest]: transcode/one "abc def"
        rest = " def"
    ]
)(
    all [
        'abc = [(void) rest]: transcode/one "abc def"
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
        all [
            (the 'A) = [^a ^b]: transcode/one "A B"
            a = the 'A
            b = the '{ B}
        ]
    )(
        a: b: ~
        all [
            (the 'A) = [^ ^b]: transcode/one "A B"
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
        all [
            <item!> = [value /rest]: transcode/one "ab cd" then [<item!>]
            value = <item!>
            rest = null
        ]
    )
    (
        value: rest: ~
        all [
            <item!> = ([value /rest]: transcode/one "ab cd") then [<item!>]
            value = 'ab
            rest = " cd"
        ]
    )

    (
        foo: func [return: [integer!] @other [integer!]] [
            other: 10
            return 20
        ]
        all [
            '~weird~ = ([^x /y]: foo then [~weird~])
            x = '~weird~
            y = null
        ]
    )

    (
        foo: func [return: [integer!] @other [integer!]] [
            other: 10
            return 20
        ]
        all [
            '~weird~ <> [^x y]: foo then [~weird~]
            x = the '20
            y = 10
        ]
    )

    (
        all [
            x: find "abc" 'b then [10]
            x = 10
            [y]: find "abc" 'b then [10]
            y = 10
        ]
    )
]

; You can use a @ without a variable to get a return result
;
(all [
    " cd" = [item @]: transcode/one "ab cd"
    item = 'ab
])

; Propagates void signals, but sets variables to null
[
    (all [
        null? [/x]: comment "hi"
        null? x
    ])
]

; Raised errors in PACKs are not legal or supported in practice.
[
    ~zero-divide~ !! (
        pack [1 / 0]
    )
    ~zero-divide~ !! (
        [^e]: pack [1 / 0]
    )
]

; Slashes represent optional return parameters
[
    ~???~ !! ([a b]: 10)
    (
        all [
            10 = [a /b]: 10
            a = 10
            b = null
        ]
    )
]

; Using @ or _ allows passthru of antiforms
[
    (
        '~(a b c)~ = ^ [x]: spread [a b c]
    )
    (
        '~(a b c)~ = ^ [@]: spread [a b c]
    )
    (
        '~(a b c)~ = ^ [_]: spread [a b c]  ; definitive behavior TBD
    )
]

; If you want to unpack an empty pack, you have to use meta to tell the
; difference between an empty pack and a null.
[
    (
        x: ~
        [^/x]: null
        x = null'
    )
    (
        x: ~
        [^/x]: comment "hi"
        x = null
    )
]
