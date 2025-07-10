; %multi.rest.r
;
; Multiple return values are based on unstable BLOCK! antiforms (PACK!).

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
        304 = [g # h]: test 1020
        g = 304
        h = <z-result>
    ])

    ; "Circling" results using FENCE! is a way of making the overall
    ; multi-return result of the expression come from another output.

    (
        a: b: c: null
        all [
            <y-result> = [a {b} c]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        all [
            304 = [{a} b c]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        all [
            <z-result> = [a b {(inside [] first [c])}]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        all [
            <z-result> = [a b {(void)}]: test 1020
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
        null? insist [[{_} _]: foo break]
    )
]

[(
    all wrap [
        'abc = [rest {_}]: transcode:next "abc def"
        rest = " def"
    ]
)(
    all wrap [
        'abc = [rest {(void)}]: transcode:next "abc def"
        rest = " def"
    ]
)(
    error? [_]: panic "a"
)]

; The ^XXX! types can be used to ask for variables to be raised to a meta
; level on assignment, and lowered via unlift on fetch
[
    (
        a: b: ~
        all [
            (the '-[ B]-) = [{^b} ^a]: transcode:next "A B"
            a = the 'A
            b = the '-[ B]-
        ]
    )(
        a: b: ~
        all [
            (the '-[ B]-) = [{^} ^a]: transcode:next "A B"
            a = the 'A
            unset? $b
        ]
    )
]

; Infix processing when using a multi-return should match the processing when
; you are using an ordinary return.
[
    (
        value: rest: ~
        all [
            "fake" = [rest :value]: transcode:next "ab cd" then ["fake"]
            value = null
            rest = "fake"
        ]
    )
    (
        value: rest: ~
        all [
            <item!> = [rest :value]: transcode:next "ab cd" then [<item!>]
            value = null
            rest = <item!>
        ]
    )

    (
        foo: func [return: [~[integer! integer!]~]] [
            return pack [20 10]
        ]
        all wrap [
            '~#weird~ = [{^x} :y]: (foo then [~#weird~])
            x = '~#weird~
            y = null
        ]
    )

    (
        foo: func [return: [~[integer! integer!]~]] [
            return pack [20 10]
        ]
        all wrap [
            '~#weird~ <> [^x y]: foo then [~#weird~]
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

; You can use a {} without a variable to get a return result
;
(all wrap [
    " cd" = [_ item]: transcode:next "ab cd"
    item = 'ab
])

; Propagates nihil signals, but sets variables to null
[
    (all wrap [
        void? [:x]: comment "hi"
        null? x
    ])
]

; Errors are not supported in the default PACK.
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
            [{^e} n]: pack* [1 / 0, 1 + 0]
            n = 1
            error? ^e
            'zero-divide = (unquasi e).id
        ]
    )
]

; Slashes represent optional return parameters
[
    ~???~ !! ([a b]: 10)
    (
        all wrap [
            10 = [a :b]: 10
            a = 10
            b = null
        ]
    )
]

[
    (
        '~(a b c)~ = lift decay [x]: spread [a b c]
    )
    (
        '~(a b c)~ = lift [{_}]: spread [a b c]
    )
    (
        '~(a b c)~ = lift decay [_]: spread [a b c]  ; definitive behavior TBD
    )
]

; If you want to unpack an empty pack, you have to use lift to tell the
; difference between an empty pack and a null.
[
    (
        x: ~
        [:^x]: null
        x = (lift null)
    )
    (
        x: ~
        [:^x]: comment "hi"
        x = null
    )
]
