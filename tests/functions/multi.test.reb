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
    (test: func [x] [
        return pack [304 <y-result> <z-result>]
    ]
    ok)

    (304 = test 1020)

    (all wrap [
        304 = [a]: test 1020
        a = 304
    ])

    (all wrap [
        304 = [b c]: test 1020
        b = 304
        c = <y-result>
    ])

    (all wrap [
        304 = [d e f]: test 1020
        d = 304
        e = <y-result>
        f = <z-result>
    ])

    (all wrap [
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
        foo: func [return: [~[integer! integer!]~] arg] [
            return pack [20 10]
        ]
        null? until [[@ _]: foo break]
    )
]

[(
    all wrap [
        'abc = [rest @]: transcode:next "abc def"
        rest = " def"
    ]
)(
    all wrap [
        'abc = [rest @(void)]: transcode:next "abc def"
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
            (the '{ B}) = [^b ^a]: transcode:next "A B"
            a = the 'A
            b = the '{ B}
        ]
    )(
        a: b: ~
        all [
            (the '{ B}) = [^ ^a]: transcode:next "A B"
            a = the 'A
            unset? $b
        ]
    )
]

; Enfix processing when using a multi-return should match the processing when
; you are using an ordinary return.
[
    (
        value: rest: ~
        all [
            "fake" = [rest /value]: transcode:next "ab cd" then ["fake"]
            value = null
            rest = "fake"
        ]
    )
    (
        value: rest: ~
        all [
            <item!> = [rest /value]: transcode:next "ab cd" then [<item!>]
            value = null
            rest = <item!>
        ]
    )

    (
        foo: func [return: [~[integer! integer!]~]] [
            return pack [20 10]
        ]
        all wrap [
            '~<weird>~ = [^x /y]: (foo then [~<weird>~])
            x = '~<weird>~
            y = null
        ]
    )

    (
        foo: func [return: [~[integer! integer!]~]] [
            return pack [20 10]
        ]
        all wrap [
            '~<weird>~ <> [^x y]: foo then [~<weird>~]
            x = the '20
            y = 10
        ]
    )

    (
        all wrap [
            x: find "abc" 'b then [10]
            x = 10
            [y]: find "abc" 'b then [10]
            y = 10
        ]
    )
]

; You can use a @ without a variable to get a return result
;
(all wrap [
    " cd" = [@ item]: transcode:next "ab cd"
    item = 'ab
])

; Propagates nihil signals, but sets variables to null
[
    (all wrap [
        null? [/x]: comment "hi"
        null? x
    ])
]

; Raised errors are not supported by the default PACK.
[
    ~zero-divide~ !! (
        pack [1 / 0]
    )
    ~zero-divide~ !! (
        [^e]: pack [1 / 0]
    )
]

; You must use PACK* to get errors
[
    (
        pack? pack* [1 / 0]
    )
    (
        all wrap [
            [^e n]: pack* [1 / 0, 1 + 0]
            n = 1
            raised? unmeta e
            'zero-divide =(noquasi e).id
        ]
    )
]

; Slashes represent optional return parameters
[
    ~???~ !! ([a b]: 10)
    (
        all wrap [
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
        x = ^null
    )
    (
        x: ~
        [^/x]: comment "hi"
        x = null
    )
]
