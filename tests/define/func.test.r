; functions/define/func.r
; recursive safety
(
    f: func [return: [action!]] [
        return lambda [x] [
            either x = 1 [
                reeval f 2
                boolean x = 1
            ][
                'false
            ]
        ]
    ]
    true? reeval f 1
)

; Check parameter processing produces correct labeling.  These were written in
; response to a specific bug, but should be expanded:
;
; https://github.com/metaeducation/ren-c/issues/1113
;
; (Though there are several issues in flux at time of writing regarding how
; DATATYPE!s and type checking work...)
[
(
    describe: lambda [f [<unrun> frame!]] [
        collect [
            let m: adjunct-of f
            keep reify all [m, m.description]

            let keep-param: lambda [k [word! ~(return:)~] p [parameter!]] [
                keep reduce [k (reify p.spec) (reify p.text)]
            ]
            keep-param 'return: (return of f)
            for-each [key param] f [
                keep-param key param
            ]
        ]
    ]
    ok
)
(
    (describe func [
        "has no return" a "a" b "b"
    ] []) = [
        "has no return"
        [return: ~null~ ~null~]
        [a ~null~ "a"]
        [b ~null~ "b"]
    ]
)(
    (describe func [
        "has return with no type" return: "returns" a "a" b "b"
    ] []) = [
        "has return with no type"
        [return: ~null~ "returns"]
        [a ~null~ "a"]
        [b ~null~ "b"]
    ]
)(
    (describe func [
        "has return with type" return: [integer!] "returns" a "a" b "b"
    ] []) = [
        "has return with type"
        [return: [integer!] "returns"]
        [a ~null~ "a"]
        [b ~null~ "b"]
    ]
)(
    (describe func [  ; try it without a description
        return: [integer!] "returns" a "a" :b "b"
    ] []) = [
        ~null~
        [return: [integer!] "returns"]
        [a ~null~ "a"]
        [b ~null~ "b"]
    ]
)]

; Literal parameter checking
[
    (
        foo: func [control [~(on off)~]] [return control]
        bar: func [decoration [~([*] (()))~]] [return decoration]
        baz: func [evil [~(~mojo~ ~jojo~ ~(mojo jojo)~)~]] [return evil]
        ok
    )

    ('on = foo 'on)
    ('off = foo 'off)
    ~expect-arg~ !! (foo 'explode)

    ([*] = bar [*])
    ('(()) = bar '(()))
    ~expect-arg~ !! (bar [**])
    ~expect-arg~ !! (bar '((())))

    ('~mojo~ = baz '~mojo~)
    ('~jojo~ = baz '~jojo~)
    ('~(mojo jojo)~ = baz lift spread [mojo jojo])
    ~expect-arg~ !! (baz lift spread [jojo mojo])
]
