; %parameter.test.r

(parameter? append.series)
(parameter? append.value)
(parameter? append.1)
(parameter? append.2)
(append.1 = append.series)
(append.2 = append.value)
(append.1 <> append.value)
(append.2 <> append.series)

; https://rebol.metaeducation.com/t/function-args-by-int/2507
(
    foo: func [arg1 :refine1 arg2 :refine2] [
        let f: binding of $arg1
        return reduce [f.1 f.2]
    ]

    [10 20] = foo 10 20
)
