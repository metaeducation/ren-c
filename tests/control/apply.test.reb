; functions/control/apply.r
; bug#44
[error? try [r3-alpha-apply 'type-of/word []]]
[1 == r3-alpha-apply :subtract [2 1]]
[1 = (r3-alpha-apply :- [2 1])]
[error? try [r3-alpha-apply func [a] [a] []]]
[error? try [r3-alpha-apply/only func [a] [a] []]]

; CC#2237
[error? try [r3-alpha-apply func [a] [a] [1 2]]]
[error? try [r3-alpha-apply/only func [a] [a] [1 2]]]

[true = r3-alpha-apply func [/a] [a] [true]]
[false == r3-alpha-apply func [/a] [a] [false]]
[false == r3-alpha-apply func [/a] [a] []]
[true = r3-alpha-apply/only func [/a] [a] [true]]
; the word 'false
[true = r3-alpha-apply/only func [/a] [a] [false]]
[false == r3-alpha-apply/only func [/a] [a] []]
[use [a] [a: true true = r3-alpha-apply func [/a] [a] [a]]]
[use [a] [a: false false == r3-alpha-apply func [/a] [a] [a]]]
[use [a] [a: false true = r3-alpha-apply func [/a] [a] ['a]]]
[use [a] [a: false true = r3-alpha-apply func [/a] [a] [/a]]]
[use [a] [a: false true = r3-alpha-apply/only func [/a] [a] [a]]]
[group! == r3-alpha-apply/only :type-of [()]]
[[1] == head r3-alpha-apply :insert [copy [] [1] blank blank blank]]
[[1] == head r3-alpha-apply :insert [copy [] [1] blank blank false]]
[[[1]] == head r3-alpha-apply :insert [copy [] [1] blank blank true]]
[function! == r3-alpha-apply :type-of [:print]]
[get-word! == r3-alpha-apply/only :type-of [:print]]
; bug#1760
[1 == eval does [r3-alpha-apply does [] [return 1] 2]]
; bug#1760
[1 == eval does [r3-alpha-apply func [a] [a] [return 1] 2]]
; bug#1760
[1 == eval does [r3-alpha-apply does [] [return 1]]]
[1 == eval does [r3-alpha-apply func [a] [a] [return 1]]]
[1 == eval does [r3-alpha-apply :also [return 1 2]]]
; bug#1760
[1 == eval does [r3-alpha-apply :also [2 return 1]]]

; EVAL/ONLY
[
    o: make object! [a: 0]
    b: eval/only (quote o/a:) 1 + 2
    all [o/a = 1 | b = 1] ;-- above acts as `b: (eval/only (quote o/a:) 1) + 2`
]
[
    a: func [b c :d] [reduce [b c d]]
    [1 + 2] = (eval/only :a 1 + 2)
]

[
    void? r3-alpha-apply func [
        return: [<opt> any-value!]
        x [<opt> any-value!]
    ][
        get/only 'x
    ][
        ()
    ]
][
    void? r3-alpha-apply func [
        return: [<opt> any-value!]
        'x [<opt> any-value!]
    ][
        get/only 'x
    ][
        ()
    ]
][
    void? r3-alpha-apply func [
        return: [<opt> any-value!]
        x [<opt> any-value!]
    ][
        return get/only 'x
    ][
        ()
    ]
][
    void? r3-alpha-apply func [
        return: [<opt> any-value!]
        'x [<opt> any-value!]
    ][
        return get/only 'x
    ][
        ()
    ]
]
[
    error? r3-alpha-apply func ['x [<opt> any-value!]] [
        return get/only 'x
    ][
        make error! ""
    ]
][
    error? r3-alpha-apply/only func [x [<opt> any-value!]] [
        return get/only 'x
    ] head insert copy [] make error! ""
][
    error? r3-alpha-apply/only func ['x [<opt> any-value!]] [
        return get/only 'x
    ] head insert copy [] make error! ""
]
[use [x] [x: 1 strict-equal? 1 r3-alpha-apply func ['x] [:x] [:x]]]
[use [x] [x: 1 strict-equal? 1 r3-alpha-apply func ['x] [:x] [:x]]]
[
    use [x] [
        x: 1
        strict-equal? first [:x] r3-alpha-apply/only func [:x] [:x] [:x]
    ]
][
    use [x] [
        unset 'x
        strict-equal? first [:x] r3-alpha-apply/only func ['x [<opt> any-value!]] [
            return get/only 'x
        ] [:x]
    ]
]
[use [x] [x: 1 strict-equal? 1 r3-alpha-apply func [:x] [:x] [x]]]
[use [x] [x: 1 strict-equal? 'x r3-alpha-apply func [:x] [:x] ['x]]]
[use [x] [x: 1 strict-equal? 'x r3-alpha-apply/only func [:x] [:x] [x]]]
[use [x] [x: 1 strict-equal? 'x r3-alpha-apply/only func [:x] [return :x] [x]]]
[
    use [x] [
        unset 'x
        strict-equal? 'x r3-alpha-apply/only func ['x [<opt> any-value!]] [
            return get/only 'x
        ] [x]
    ]
]
