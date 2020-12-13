; %enfix.test.reb

(action! = type of :+)
(true = enfixed? :+)

(enfixed? :+)
(error? trap [enfixed? 1])
(action? get '+)

; #1934
(error? trap [do reduce [1 get '+ 2]])
(3 = do reduce [:+ 1 2])


(
    foo: :+
    did all [
        enfixed? :foo
        3 = (1 foo 2)
    ]
)
(
    set 'foo enfixed :add
    did all [
        enfixed? :foo
        1 foo 2 = 3
    ]
)
(
    set 'postfix-thing enfixed func [x] [x * 2]
    all [
       enfixed? :postfix-thing
       20 = (10 postfix-thing)
    ]
)

(3 == do reduce [get '+ 1 2])


; Only hard-quoted parameters are <skip>-able
(
    error? trap [bad-skippy: func [x [<skip> integer!] y] [reduce [try :x y]]]
)

[
    (
        skippy: func ['x [<skip> integer!] y] [reduce [try :x y]]
        lefty: enfixed :skippy
        true
    )

    ([_ "hi"] = skippy "hi")
    ([10 "hi"] = skippy 10 "hi")

    ([_ "hi"] = lefty "hi")
    ([1 "hi"] = 1 lefty "hi")

    ; Enfixed skipped left arguments mean that a function will not be executed
    ; greedily...it will run in its own step, as if the left was an end.
    (
        unset 'var
        block: [<tag> lefty "hi"]
        did all [
            [lefty "hi"] = block: evaluate/result block 'var
            <tag> = var
            [] = evaluate/result block 'var
            [_ "hi"] = var
        ]
    )

    ; Normal operations quoting rightward outrank operations quoting left,
    ; making the left-quoting operation see nothing on the left, even if the
    ; type matched what it was looking for.
    (
        unset 'var
        block: [lit 1 lefty "hi"]
        did all [
            [lefty "hi"] = block: evaluate/result block 'var
            1 = var
            [] evaluate/result block 'var
            [_ "hi"] = var
        ]
    )

    ([_ "hi"] = any [false blank lefty "hi"])
]


; ->- is the "SHOVE" operation.  It lets any ACTION!, including one dispatched
; from PATH!, receive its first argument from the left.  It uses the parameter
; conventions of that argument.

; NORMAL parameter
;
(9 = (1 + 2 ->- multiply 3))
(7 = (add 1 2 ->- multiply 3))
(7 = (add 1 2 ->- (:multiply) 3))

; :HARD-QUOTE parameter
(
    x: _
    x: ->- default [10 + 20]
    x: ->- default [1000000]
    x = 30
)

; SHOVE should be able to handle refinements and contexts.
[
    (did obj: make object! [
        magic: enfixed func [a b /minus] [
            either minus [a - b] [a + b]
        ]
    ])

    (error? trap [1 obj/magic 2])

    (3 = (1 ->- obj/magic 2))
    (-1 = (1 ->- obj/magic/minus 2))
]


; PATH! cannot be directly quoted left, must use ->-

[
    (
        left-lit: enfixed :lit
        o: make object! [i: 10 f: does [20]]
        true
    )

    ((trap [o/i left-lit])/id = 'literal-left-path)
    (o/i ->- left-lit = 'o/i)

    ((trap [o/f left-lit])/id = 'literal-left-path)
    (o/f ->- left-lit = 'o/f)
]

; Rather than error when SET-WORD! or SET-PATH! are used as the left hand
; side of a -> operation going into an operation that evaluates its left,
; the value of that SET-WORD! or SET-PATH! is fetched and passed right, then
; written back into the variable.

(
    x: 10
    x: me + 20
    x = 30
)(
    o: make object! [x: 10]
    count: 0
    o/(count: count + 1 'x): me + 20
    (o/x = 30) and (count = 1)  ; shouldn't double-evaluate path group
)


; Right enfix always wins over left, unless the right is at array end

((lit ->-) = first [->-])
((lit ->- lit) = 'lit)
('x = (x >- lit))
(1 = (1 ->- lit))

(1 = (1 >- lit))
('x = (x >- lit))

; "Precedence" manipulation via >- and ->-

(9 = (1 + 2 ->- multiply 3))
(9 = (1 + 2 >- multiply 3))
(9 = (1 + 2 >-- lib/* 3))
(9 = (1 + 2 ->- lib/* 3))

(7 = (add 1 2 * 3))
(7 = (add 1 2 ->- lib/* 3))
(7 = (add 1 2 >- lib/* 3))

((trap [10 ->- lib/= 5 + 5])/id = 'expect-arg)
((trap [10 >- lib/= 5 + 5])/id = 'expect-arg)
(10 >-- lib/= 5 + 5)

((trap [add 1 + 2 >- multiply 3])/id = 'no-arg)
(
    x: add 1 + 2 3 + 4 >- multiply 5
    x = 38
)
(-38 = (negate x: add 1 + 2 3 + 4 >- multiply 5))
(
    (trap [divide negate x: add 1 + 2 3 + 4 >- multiply 5])/id = 'no-arg
)
(-1 = (divide negate x: add 1 + 2 3 + 4  2 >- multiply 5))


(
    (x: add 1 add 2 3 |> lib/* 4)
    x = 24
)

(
    count: 0
    o: make object! [x: _]
    nuller: function [y] [null]
    o/(count: count + 1, first [x]): my nuller
    did all [
        :o/x = null
        count = 1
    ]
)

[
    https://github.com/metaeducation/ren-c/issues/581

    (
        foo: func [] [
            fail "foo should not run, it's prefix and runs on *next* step"]
        did all [
            [foo 304] == evaluate/result [1020 foo 304] 'var
            var == 1020
        ]
    )(
        enfoo: enfixed func [] [<enfoo>]
        did all [
            [304] == evaluate/result [1020 enfoo 304] 'var
            var == <enfoo>
        ]
        comment "0-arity function, but enfixed so runs in *same* step"
    )

    (
        ifoo: func ['i [<skip> integer!]] [
            fail "ifoo should not run, it tests <skip> on *next* step"
        ]
        did all [
            ignored: func [] [
                ignored: _
                return #ignored
            ]
            did all [
                [ifoo 304] == evaluate/result [ignored ifoo 304] 'var
                var == #ignored
                ignored == _
            ]
        ]
    )(
        enifoo: enfixed func ['i [<skip> integer!]] [
            fail [
                {enifoo should not run; when arguments are skipped this}
                {defers the enfix until the next evaluator step.  Otherwise}
                {`case [1 = 1 [print "good"]] else [print "bad"]`}
                {would print both `good` and `bad`.}
            ]
        ]
        did all [
            ignored: func [] [
                ignored: _
                return #ignored
            ]
            did all [
                [enifoo 304] == evaluate/result [ignored enifoo 304] 'var
                var == #ignored
                ignored == _
            ]
        ]
    )(
        enifoo: enfixed func ['i [<skip> integer!]] [compose '<enifoo>/(i)]
        did all [
            did all [
                [304] == evaluate/result [1020 enifoo 304] 'var
                var == '<enifoo>/1020
            ]
            comment {
                When arguments are not skipped, the behavior should be the
                same as a non-skippable enfix function
            }
        ]
    )

    (
        bar: func [return: <elide>] [bar: _]
        did all [
            [bar 304] == evaluate/result [1020 bar 304] 'var
            var == 1020
            action? :bar
        ]
        comment {Invisible normal arity-0 function should run on next eval}
    )(
        enbar: enfixed func [return: <elide>] [enbar: _]
        did all [
            [304] == evaluate/result [1020 enbar 304] 'var
            var == 1020
            enbar == _
        ]
        comment {Invisible enfix arity-0 function should run on same step}
    )

    (
        ibar: func ['i [<skip> integer!]] [ibar: _]
        did all [
            ignored: func [] [
                ignored: _
                return #ignored
            ]
            did all [
                [ibar 304] == evaluate/result [ignored ibar 304] 'var
                var == #ignored
                ignored == _
            ]
            comment {skip irrelevant (tests right on *next* step)}
        ]
    )(
        enibar: enfixed func [return: <elide> 'i [<skip> integer!]] [
            fail {
                When arguments are skipped, this defers the enfix until the
                next evaluator step.  Doing otherwise would mean that
                `case [1 = 1 [print "good"]] else [print "bad"]` would
                print both `good` and `bad`.
            }
        ]
        did all [
            kept: func [] [
                kept: _
                return #kept
            ]
            did all [
                [enibar 304] == evaluate/result [kept enibar 304] 'var
                var == #kept
                kept == _
            ]
        ]
    )(
        enibar: enfixed func [return: <elide> 'i [<skip> integer!]] [
            enibar: _
        ]
        did all [
            did all [
                [304] == evaluate/result [1020 enibar 304] 'var
                var == 1020
                enibar == _
            ]
            comment {
                When arguments are not skipped, the behavior should be the
                same as a non-skippable enfix function
            }
        ]
    )
]

; Parameters in-between soft quoted functions (one trying to quote right and
; one trying to quote left) will be processed by the right hand function
; first.
[
    (
        rightq: func [:x] [compose [<rightq> was (x)]]
        leftq: enfixed func [:y] [compose [<leftq> was (y)]]

        [<rightq> was [<leftq> was foo]] = rightq foo leftq
    )(
        rightq: func [:x] [compose [<rightq> was (x)]]
        leftq: enfixed func ['y] [compose [<leftq> was (y)]]

        [<rightq> was [<leftq> was foo]] = rightq foo leftq
    )

    ((1 then x -> [x * 10]) = 10)
]


(
    a: [304]
    a/1: me / 2
    a = [152]
)
