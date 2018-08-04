; %enfix.test.reb

(action! = type of :+)
(true = enfixed? '+)

(
    foo: :+
    did all [
        not enfixed? 'foo
        error? trap [1 foo 2]
        3 = foo 1 2
    ]
)
(
    set/enfix 'foo :+
    did all [
        enfixed? 'foo
        3 = 1 foo 2
    ]
)
(
    set/enfix 'postfix-thing func [x] [x * 2]
    all [
       enfixed? 'postfix-thing
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
        skippy: func [:x [<skip> integer!] y] [reduce [try :x y]]
        lefty: enfix :skippy
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
            [lefty "hi"] = block: evaluate/set block 'var
            <tag> = var
            [] = evaluate/set block 'var
            [_ "hi"] = var
        ]
    )

    ; Normal operations quoting rightward outrank operations quoting left,
    ; making the left-quoting operation see nothing on the left, even if the
    ; type matched what it was looking for.
    (
        unset 'var
        block: [quote 1 lefty "hi"]
        did all [
            [lefty "hi"] = block: evaluate/set block 'var
            1 = var
            [] evaluate/set block 'var
            [_ "hi"] = var
        ]
    )

    ([_ "hi"] = any [false blank lefty "hi"])
]
