; functions/control/apply.r
[#44 (
    error? trap [applique 'append/only [copy [a b] 'c]]
)]
(1 == applique :subtract [2 1])
(1 = (applique :- [2 1]))
(error? trap [applique func [a] [a] []])
(error? trap [applique/only func [a] [a] []])

; CC#2237
(error? trap [applique func [a] [a] [1 2]])
(error? trap [applique/only func [a] [a] [1 2]])

(error? applique :make [error! ""])

(/a = applique func [/a] [a] [true])
(_ = applique func [/a] [a] [false])
(_ = applique func [/a] [a] [])
(/a = applique/only func [/a] [a] [true])
; the word 'false
(/a = applique/only func [/a] [a] [false])
(_ == applique/only func [/a] [a] [])
(use [a] [a: true /a = applique func [/a] [a] [a]])
(use [a] [a: false _ == applique func [/a] [a] [a]])
(use [a] [a: false /a = applique func [/a] [a] ['a]])
(use [a] [a: false /a = applique func [/a] [a] [/a]])
(use [a] [a: false /a = applique/only func [/a] [a] [a]])
(group! == applique/only (specialize 'of [property: 'type]) [()])
([1] == head of applique :insert [copy [] [1] blank blank blank])
([1] == head of applique :insert [copy [] [1] blank blank false])
([[1]] == head of applique :insert [copy [] [1] blank blank true])
(action! == applique (specialize 'of [property: 'type]) [:print])
(get-word! == applique/only (specialize 'of [property: 'type]) [:print])

;-- #1760 --

(
    1 == eval func [] [applique does [] [return 1] 2]
)
(
    1 == eval func [] [applique func [a] [a] [return 1] 2]
)
(
    1 == eval func [] [applique does [] [return 1]]
)
(
    1 == eval func [] [applique func [a] [a] [return 1]]
)
(
    1 == eval func [] [applique func [a b] [a] [return 1 2]]
)
(
    1 == eval func [] [applique func [a b] [a] [2 return 1]]
)

; EVAL/ONLY
(
    o: make object! [a: 0]
    b: eval/only (quote o/a:) 1 + 2
    all [o/a = 1 | b = 1] ;-- above acts as `b: (eval/only (quote o/a:) 1) + 2`
)
(
    a: func [b c :d] [reduce [b c d]]
    [1 + 2] = (eval/only :a 1 + 2)
)

(
    void? applique func [
        return: [<opt> any-value!]
        x [<opt> any-value!]
    ][
        get 'x
    ][
        ()
    ]
)
(
    void? applique func [
        return: [<opt> any-value!]
        'x [<opt> any-value!]
    ][
        get 'x
    ][
        ()
    ]
)
(
    void? applique func [
        return: [<opt> any-value!]
        x [<opt> any-value!]
    ][
        return get 'x
    ][
        ()
    ]
)
(
    void? applique func [
        return: [<opt> any-value!]
        'x [<opt> any-value!]
    ][
        return get 'x
    ][
        ()
    ]
)
(
    error? applique func ['x [<opt> any-value!]] [
        return get 'x
    ][
        make error! ""
    ]
)
(
    error? applique/only func [x [<opt> any-value!]] [
        return get 'x
    ] head of insert copy [] make error! ""
)
(
    error? applique/only func ['x [<opt> any-value!]] [
        return get 'x
    ] head of insert copy [] make error! ""
)
(use [x] [x: 1 strict-equal? 1 applique func ['x] [:x] [:x]])
(use [x] [x: 1 strict-equal? 1 applique func ['x] [:x] [:x]])
(
    use [x] [
        x: 1
        strict-equal? first [:x] applique/only func [:x] [:x] [:x]
    ]
)
(
    use [x] [
        unset 'x
        strict-equal? first [:x] applique/only func ['x [<opt> any-value!]] [
            return get 'x
        ] [:x]
    ]
)
(use [x] [x: 1 strict-equal? 1 applique func [:x] [:x] [x]])
(use [x] [x: 1 strict-equal? 'x applique func [:x] [:x] ['x]])
(use [x] [x: 1 strict-equal? 'x applique/only func [:x] [:x] [x]])
(use [x] [x: 1 strict-equal? 'x applique/only func [:x] [return :x] [x]])
(
    use [x] [
        unset 'x
        strict-equal? 'x applique/only func ['x [<opt> any-value!]] [
            return get 'x
        ] [x]
    ]
)

; MAKE FRAME! :RETURN should preserve binding in the FUNCTION OF the frame
;
(1 == eval func [] [applique :return [1] 2])

(_ == applique/only func [/a] [a] [#[false]])
(group! == applique/only :type-of [()])
