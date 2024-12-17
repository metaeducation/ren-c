; functions/define/func.r
; recursive safety
(
    /f: func [return: [action?]] [
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
    /get-params: func [f [frame!]] [
        return map-each [key param] f [
            assert [parameter? param]
            reduce [key (reify param.spec) (reify param.text)]
        ]
    ]
    ok
)
(
    all wrap [  ; try with no RETURN:
        f: meta:lite func ["description" a "a" b "b"] []
        m: adjunct-of f
        m.description = "description"
        null = return of f
        (get-params f) = [
            [a ~null~ "a"]
            [b ~null~ "b"]
        ]
    ]
)(
    all wrap [  ; try RETURN: with no type
        f: meta:lite func ["description" return: "returns" a "a" b "b"] []
        m: adjunct-of f
        m.description = "description"
        r: return of f
        r.spec = null
        r.text = "returns"
        (get-params f) = [
            [a ~null~ "a"]
            [b ~null~ "b"]
        ]
    ]
)(
    all wrap [  ; try RETURN: with type
        f: meta:lite func [
            "description" return: [integer!] "returns" a "a" b "b"
        ][
        ]
        m: adjunct-of f
        m.description = "description"
        r: return of f
        r.spec = [integer!]
        r.text = "returns"
        (get-params f) = [
            [a ~null~ "a"]
            [b ~null~ "b"]
        ]
    ]
)(
    all wrap [  ; try without description
        f: meta:lite func [return: [integer!] "returns" a "a" :b "b"] []
        if m: adjunct-of f [
            m.description = null
        ]
        r: return of f
        r.spec = [integer!]
        r.text = "returns"
        (get-params f) = [
            [a ~null~ "a"]
            [b ~null~ "b"]
        ]
    ]
)]

; Literal parameter checking
[
    (
        /foo: func [control ['on 'off]] [return control]
        /bar: func [decoration ['[*] '(())]] [return decoration]
        /baz: func [evil ['~mojo~ '~jojo~ '~(mojo jojo)~]] [return evil]
        /mumble: func [splice [~(a b c)~]] [return splice]
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
    ('~(mojo jojo)~ = baz meta spread [mojo jojo])
    ~expect-arg~ !! (baz meta spread [jojo mojo])

    (~(a b c)~ = mumble ~(a b c)~)
    ~expect-arg~ !! (mumble ~(a b c d)~)
]
