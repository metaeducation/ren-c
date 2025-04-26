; %infix.test.reb

(action! = type of +/)
(okay = infix? +/)

(
    foo: :+
    all [
        infix? foo/
        1 foo 2 = 3
    ]
)
(
    foo: infix :add
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


; Only hard-quoted parameters are <skip>-able
(
    error? sys/util/rescue [bad-skippy: lambda [x [<skip> integer!] y] [reduce [reify :x y]]]
)

[
    (
        skippy: lambda [:x [<skip> integer!] y] [reduce [reify :x y]]
        lefty: infix :skippy
        okay
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
        all [
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
        all [
            [lefty "hi"] = block: evaluate/step3 block 'var
            1 = var
            [] evaluate/step3 block 'var
            [~null~ "hi"] = var
        ]
    )

    ([~null~ "hi"] = any [null null lefty "hi"])
]
