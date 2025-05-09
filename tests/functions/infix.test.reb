; %infix.test.reb

(action! = type of +/)
(infix? +/)

(infix? +/)
~expect-arg~ !! (infix? 1)
(action? +/)

; #1934
(3 = eval reduce [1 unrun +/ 2])
~no-arg~ !! (eval reduce [unrun +/ 1 2])


(
    foo: +/
    all [
        infix? foo/
        3 = (1 foo 2)
    ]
)
(
    foo: infix add/
    all [
        infix? foo/
        1 foo 2 = 3
    ]
)
(
    postfix-thing: infix lambda [x] [x * 2]
    all [
       infix? postfix-thing/
       20 = (10 postfix-thing)
    ]
)

~no-arg~ !! (eval reduce [unrun +/ 1 2])  ; infix no argument


(
    x: 10
    x: me + 20
    x = 30
)

; !!! ME is currently macro-based and double evaluates the group...once for
; the GET and once for the SET.  This is undesirable.
(
    o: make object! [x: 10]
    count: 0
    o.(count: count + 1 'x): me + 20
    (o.x = 30) and (count = 2)  ; !!! count should only be 1
)

(
    count: 0
    o: make object! [x: null]
    nuller: func [y] [return null]
    o.(count: count + 1, first [x]): my nuller
    all [
        :o.x = null
        count = 2
    ]
)

[
    https://github.com/metaeducation/ren-c/issues/581

    (
        foo: func [] [
            panic "foo should not run, it's prefix and runs on *next* step"
        ]
        all wrap [
            1020 = [pos {#}]: evaluate:step [1020 foo 304]
            pos = [foo 304]
        ]
    )(
        i-foo: infix func [
            "0-arity function, but infix so runs in *same* step"
        ][
            return <i-foo>
        ]
        all wrap [
            <i-foo> = [pos {#}]: evaluate:step [1020 i-foo 304]
            pos = [304]
        ]
    )

    (
        bar: func [
            "Invisible normal arity-0 function should run on next eval"
            return: [ghost!]
        ][
            bar: null
            return ~,~
        ]
        all wrap [
            [pos var]: evaluate:step [1020 bar 304]
            pos = [bar 304]
            var = 1020
            action? bar/
            bar
            null? bar
        ]
    )(
        i-bar: infix func [
            "Invisible infix arity-0 function should run on same step"
            left
        ][
            i-bar: null
            return left
        ]
        all wrap [
            [pos var]: evaluate:step [1020 i-bar 304]
            pos = [304]
            var = 1020
            null? i-bar
        ]
    )
]

; Parameters in-between soft quoted functions (one trying to quote right and
; one trying to quote left) will be processed by the right hand function
; first.
[
    (
        rightq: lambda [@(x)] [compose [<rightq> was (x)]]
        leftq: infix lambda [@(y)] [compose [<leftq> was (y)]]

        [<rightq> was [<leftq> was foo]] = rightq foo leftq
    )(
        rightq: lambda [@(x)] [compose [<rightq> was (x)]]
        leftq: infix lambda ['y] [compose [<leftq> was (y)]]

        [<rightq> was [<leftq> was foo]] = rightq foo leftq
    )

    ((1 then x -> [x * 10]) = 10)
]


(
    a: [304]
    a.1: me / 2
    a = [152]
)
