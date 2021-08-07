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
    (test: func [x y: z:] [
        if not null? y [
            assert [bad-word? friendly get/any y]
            set y <y-result>
        ]
        if not null? z [
            assert [bad-word? friendly get/any z]
            set z <z-result>
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

    ; "Circling" results using SYM-XXX! is a way of making the overall
    ; multi-return result of the expression come from another output.

    (
        a: b: c: null
        did all [
            <y-result> = [a ^b c]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        did all [
            304 = [^a b c]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        did all [
            <z-result> = [a b ^(first [c])]: test 1020
            a = 304
            b = <y-result>
            c = <z-result>
        ]
    )(
        a: b: c: null
        did all [
            <z-result> = [a b ^(#)]: test 1020
            a = 304
            b = <y-result>
            c = null
        ]
    )
]

[
    (
        foo: func [return: [integer!] other: [integer!] arg] [
            set other 10
            return 20
        ]
        null? until [[# #]: foo break]
    )
]
