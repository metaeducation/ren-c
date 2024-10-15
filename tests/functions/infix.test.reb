; %infix.test.reb

(action! = type of +/)
(true = infix? +/)

(
    foo: :+
    did all [
        infix? foo/
        3 = 1 foo 2
    ]
)
(
    foo: infix tighten :add
    did all [
        infix? foo/
        3 = 1 foo 2
    ]
)
(
    postfix-thing: infix func [x] [x * 2]
    all [
       infix? postfix-thing/
       20 = (10 postfix-thing)
    ]
)


; Only hard-quoted parameters are <skip>-able
(
    error? sys/util/rescue [bad-skippy: func [x [<skip> integer!] y] [reduce [reify :x y]]]
)

[
    (
        skippy: func [:x [<skip> integer!] y] [reduce [reify :x y]]
        lefty: infix :skippy
        true
    )

    ([~null~ "hi"] = skippy "hi")
    ([10 "hi"] = skippy 10 "hi")

    ([~null~ "hi"] = lefty "hi")
    ([1 "hi"] = 1 lefty "hi")

    ; Enfixed skipped left arguments mean that a function will not be executed
    ; greedily...it will run in its own step, as if the left was an end.
    (
        var: ~
        block: [<tag> lefty "hi"]
        did all [
            [lefty "hi"] = block: evaluate/step3 block 'var
            <tag> = var
            [] = evaluate/step3 block 'var
            [~null~ "hi"] = var
        ]
    )

    ; Normal operations quoting rightward outrank operations quoting left,
    ; making the left-quoting operation see nothing on the left, even if the
    ; type matched what it was looking for.
    (
        var: ~
        block: [the 1 lefty "hi"]
        did all [
            [lefty "hi"] = block: evaluate/step3 block 'var
            1 = var
            [] evaluate/step3 block 'var
            [~null~ "hi"] = var
        ]
    )

    ([~null~ "hi"] = any [false blank lefty "hi"])
]


; >- is the "SHOVE" operation.  It lets any ACTION!...including one dispatched
; from PATH!, receive its first argument from the left.  It uses the parameter
; conventions of that argument.

; NORMAL parameter
;
(9 = (1 + 2 >- multiply 3))
(9 = (add 1 2 >- multiply 3))
(9 = (add 1 2 >- (:multiply) 3))

; #TIGHT parameter
;
(9 = 1 + 2 >- * 3)
(7 = add 1 2 >- * 3)
(7 = add 1 2 >- (:*) 3)

; :HARD-QUOTE parameter
(
    x: _
    x: >- default [10 + 20]
    x: >- default [1000000]
    x = 30
)

; SHOVE should be able to handle refinements and contexts.
[
    (did obj: make object! [
        magic: infix func [#a #b /minus] [
            either minus [a - b] [a + b]
        ]
    ])

    (error? sys/util/rescue [1 obj/magic 2])

    (3 = 1 >- obj/magic 2)
    (-1 = 1 >- obj/magic/minus 2)
]
