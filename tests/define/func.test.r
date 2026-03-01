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
    describe: lambda [f [frame!]] [
        collect [
            let keep-param: lambda [k [word! ~[return:]~] p [parameter!]] [
                keep reduce [k (reify p.spec) (reify p.text)]
            ]
            keep-param 'return: (return of f)
            for-each [key param] f/ [  ; F/ gets PARAMETER!, not nulls (holes)
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
        [return: ~null~ "has no return"]
        [a ~null~ "a"]
        [b ~null~ "b"]
    ]
)(
    (describe func [
        "has divergent return" return: [] a "a" b "b"
    ] []) = [
        [return: [] "has divergent return"]
        [a ~null~ "a"]
        [b ~null~ "b"]
    ]
)(
    (describe func [
        "has return with type" return: [integer!] a "a" b "b"
    ] []) = [
        [return: [integer!] "has return with type"]
        [a ~null~ "a"]
        [b ~null~ "b"]
    ]
)(
    (describe func [  ; try it without a description
        return: [integer!] a "a" :b "b"
    ] []) = [
        [return: [integer!] ~null~]
        [a ~null~ "a"]
        [b ~null~ "b"]
    ]
)]

; Literal parameter checking
[
    (
        foo: func [control [~[on off]~]] [return control]
        bar: func [decoration [~[[*] (())]~]] [return decoration]
        baz: func [evil [~[~mojo~ ~jojo~ ~[mojo jojo]~]~]] [return evil]
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
    ('~[mojo jojo]~ = baz lift spread [mojo jojo])
    ~expect-arg~ !! (baz lift spread [jojo mojo])
]
