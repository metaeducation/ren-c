; datatypes/module.r

(module? module [] [])
(not module? 1)
(module! = kind of module [] [])

; Create a Module, but Don't Import It
(
    var: <before>
    m: a-module: module [
    ][
        var: <inner>
    ]
    all [
        var = <before>
        m.var = <inner>

        elide var: <outer>
        var = <outer>
        m.var = <inner>
    ]
)

; Import a Module without an Exported Variable
(
    var: <before>
    m: import mm: module [
    ][
        var: <inner>
    ]
    all [
        m = mm
        var = <before>
        m.var = <inner>

        elide var: <outer>
        var = <outer>
        m.var = <inner>
    ]
)

; Import a Module with an Exported Variable
(
    var: <before>
    m: import mm: module [
        exports: [var]
    ][
        var: <inner>
    ]
    all [
        m = mm
        m.var = <inner>
        var = <inner>  ; imported

        ; Overwriting m.var will not have an effect on our copy of the
        ; variable.
        ;
        elide m.var: <overwritten>
        m.var = <overwritten>
        var = <inner>

        ; The variables are fully disconnected post-import.
        ;
        elide var: <outer>
        var = <outer>
        m.var = <overwritten>
    ]
)

; Copy a Module
(
    m1: module [] [a: 10 b: 20]
    m2: copy m1
    all [
        m1.a = m2.a
        m1.b = m2.b
        m1 = m2
    ]
)

; Import a module to a second module without contaminating the first
(
    var: <outer>

    m1: module [Exports: [var]] [var: <1>]
    m2: module [Exports: [fetch]] compose [
        var: <2>
        import (<m2compose> m1)
        fetch: does [var]
    ] <m2compose>
    all [
        var = <outer>
        <1> = m2/fetch
    ]
)

; Overwrite a lib definition but make a helper that runs code in lib
(
    eval-before: :eval
    import m: module [Exports: [test]] [
        eval: func [source] [throw <override>]

        emulate: func [source [block!] <local> rebound] [
            assert ['eval = first source]

            rebound: inside lib bindable source
            return lib/eval rebound
        ]

        test: does [emulate [eval [1 + 2]]]
    ]
    all [
        3 = test
        <override> = catch [m/eval [1 + 2]]
        ^eval = ^eval-before
    ]
)

; !!! This was the only R3-Alpha RESOLVE test relating to a bug with usage
; of both the /EXTEND and /ONLY refinements.  Those refinements do not exist
; at the time of writing, and the function has been renamed to PROXY-EXPORTS
;
[#2017
    (get has proxy-exports (module [] []) (module [] [a: true]) [a] 'a)
]
