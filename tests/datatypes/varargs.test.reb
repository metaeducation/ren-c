(
    foo: lambda [x [integer! <variadic>]] [
        let sum: 0
        while [not tail? x] [
            sum: sum + take x
        ]
        sum
    ]
    z: ~
    y: (z: foo 1 2 3, 4 5)
    all [y = 5, z = 6, 0 = (foo)]
)
(
    foo: func [x [integer! <variadic>]] [return make block! x]
    [1 2 3 4] = foo 1 2 3 4
)

; leaked VARARGS! cannot be accessed after call is over
;
~frame-not-on-stack~ !! (
    take reeval (unrun lambda [x [integer! <variadic>]] [x])
)

(
    f: func [args [any-value? <variadic>]] [
       let b: take args
       return either tail? args [b] ["not at end"]
    ]
    x: make varargs! [~null~]
    null? applique f/ [args: x]
)

(
    f: lambda ['look [<variadic>]] [try first look]
    null? applique f/ [look: make varargs! []]
)

; !!! Experimental behavior of infix variadics, is to act as either 0 or 1
; items.  0 is parallel to <end>, and 1 is parallel to a single parameter.
; It's a little wonky because the evaluation of the parameter happens *before*
; the TAKE is called, but theorized that's still more useful than erroring.
[
    (
        normal: infix func [return: [integer!] v [integer! <variadic>]] [
            let sum: 0
            while [not tail? v] [
                sum: sum + take v
            ]
            return sum + 1
        ]
        ok
    )

    (1 = eval [normal])
    (11 = eval [10 normal])
    (21 = eval [10 20 normal])
    (31 = eval wrap [x: 30, y: 'x, 1 2 x normal])
    (30 = eval [multiply 3 9 normal])  ; seen as ((multiply 3 (9 normal))
][
    (
        defers: infix:defer func [return: [integer!] v [integer! <variadic>]] [
            let sum: 0
            while [not tail? v] [
                sum: sum + take v
            ]
            return sum + 1
        ]
        ok
    )

    (1 = eval [defers])
    (11 = eval [10 defers])
    (21 = eval [10 20 defers])
    (31 = eval wrap [x: 30, y: 'x, 1 2 x defers])
    (28 = eval [multiply 3 9 defers])  ; seen as (multiply 3 9) defers))
][
    (
        soft: infix func [@(v) [any-value? <variadic>]] [
            return collect [
                while [not tail? v] [
                    keep take v
                ]
            ]
        ]
        ok
    )

    ([] = eval [soft])
    ~literal-left-path~ !! (
        a: ~null~
        (a soft)
    )
    ([7] = eval [(1 + 2) (3 + 4) soft])
][
    (
        hard: infix func ['v [any-value? <variadic>]] [
            return collect [
                while [not tail? v] [
                    keep take v
                ]
            ]
        ]
        ok
    )

    ([] = eval [hard])
    ~literal-left-path~ !! (
        a: ~null~
        (a hard)
    )
    ([:(3 + 4)] = eval [:(1 + 2) :(3 + 4) hard])
]


; Testing the variadic behavior of |> and <| is easier than rewriting tests
; here to do the same thing.

; <| and |> were originally infix, so the following tests would have meant x
; would be unset
(
    value: ~
    x: ~

    3 = (value: 1 + 2 <| 30 + 40 x: value  () ())

    all [value = 3, x = 3]
)

(
    1 = (1 <| 2, 3 + 4, 5 + 6)
)

; WATERSHED TEST: This involves the parity of variadics with normal actions,
; showing that simply taking arguments in order gives compatible results.
;
; https://github.com/metaeducation/ren-c/issues/912

(
    vblock: collect wrap [
        log: adapt keep/ [set:any $value spread reduce value]
        variadic2: func [return: [text!] v [any-value? <variadic>]] [
           log [<1> take v]
           log [<2> take v]
           if not tail? v [panic "THEN SHOULD APPEAR AS IF IT IS VARARGS END"]
           return "returned"
       ]
       result: variadic2 "a" "b" then t -> [log [<t> t] "then"]
       log [<result> result]
    ]

    nblock: collect wrap [
        log: adapt keep/ [set:any $value spread reduce value]
        normal2: func [return: [text!] n1 n2] [
            log [<1> n1 <2> n2]
            return "returned"
        ]
        result: normal2 "a" "b" then t -> [log [<t> t] "then"]
        log [<result> result]
    ]

    all [
        vblock = [<1> "a" <2> "b" <t> "returned" <result> "then"]
        vblock = nblock
    ]
)


; This used to be code in the boot process.  It was needed to be variadic in
; order to quote top-level words.  It's done more efficiently now, but this
; tests a pretty weird piece of functionality...so preserved here.
(
    run func [
        return: [~]
        @set-words [<variadic> set-word? tag!]
        <local>
            set-word type-name tester meta
    ][
        while [<end> != set-word: take set-words] [
            type-name: parse as text! unchain set-word [
                between "usermode-" "?"
            ]
            append type-name "!"
            set set-word /tester: lambda [value] compose [
                (get inside lib (as word! type-name)) = type of :value
            ]
            set-adjunct tester/ make system.standard.action-adjunct [
                description: spaced ["Return TRUE if value is" an type-name]
                return-type: [logic?]
            ]
        ]
    ]
        usermode-integer?:
        usermode-block?:
        usermode-tuple?:
        <end>

    all [
        usermode-integer? 1
        not usermode-block? 1
        usermode-tuple? 'a.b.c
    ]
)
