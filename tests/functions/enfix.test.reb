; %enfix.test.reb

(antiform! = type of :+)
(enfix? :+)

(enfix? :+)
~expect-arg~ !! (enfix? 1)
(action? get $+)

; #1934
(3 = eval reduce [1 unrun get $+ 2])
~no-arg~ !! (eval reduce [unrun :+ 1 2])


(
    foo: :+
    all [
        enfix? :foo
        3 = (1 foo 2)
    ]
)
(
    set $foo enfix :add
    all [
        enfix? :foo
        1 foo 2 = 3
    ]
)
(
    set $postfix-thing enfix lambda [x] [x * 2]
    all [
       enfix? :postfix-thing
       20 = (10 postfix-thing)
    ]
)

~no-arg~ !! (eval reduce [unrun get $+ 1 2])  ; enfix no argument


; Rather than error when SET-WORD! or SET-PATH! are used as the left hand
; side of a -> operation going into an operation that evaluates its left,
; the value of that SET-WORD! or SET-PATH! is fetched and passed right, then
; written back into the variable.

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
            fail "foo should not run, it's prefix and runs on *next* step"]
        all [
            1020 == [pos @]: evaluate/next [1020 foo 304]
            pos == [foo 304]
        ]
    )(
        enfoo: enfix func [] [return <enfoo>]
        all [
            <enfoo> == [pos @]: evaluate/next [1020 enfoo 304]
            pos = [304]
        ]
        comment "0-arity function, but enfixed so runs in *same* step"
    )

    (
        bar: func [return: [nihil?]] [bar: null, return nihil]
        all [
            [pos var]: evaluate/next [1020 bar 304]
            pos = [bar 304]
            var == 1020
            action? :bar
            bar
            null? bar
        ]
        comment {Invisible normal arity-0 function should run on next eval}
    )(
        enbar: enfix func [left] [enbar: null, return left]
        all [
            [pos var]: evaluate/next [1020 enbar 304]
            pos = [304]
            var == 1020
            null? enbar
        ]
        comment {Invisible enfix arity-0 function should run on same step}
    )
]

; Parameters in-between soft quoted functions (one trying to quote right and
; one trying to quote left) will be processed by the right hand function
; first.
[
    (
        rightq: lambda [':x] [compose [<rightq> was (x)]]
        leftq: enfix lambda [':y] [compose [<leftq> was (y)]]

        [<rightq> was [<leftq> was foo]] = rightq foo leftq
    )(
        rightq: lambda [':x] [compose [<rightq> was (x)]]
        leftq: enfix lambda ['y] [compose [<leftq> was (y)]]

        [<rightq> was [<leftq> was foo]] = rightq foo leftq
    )

    ((1 then x -> [x * 10]) = 10)
]


(
    a: [304]
    a.1: me / 2
    a = [152]
)
