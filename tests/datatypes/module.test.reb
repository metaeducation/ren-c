; datatypes/module.r

(module? module [] [])
(not module? 1)
(module! = type of module [] [])

; Create a Module, but Don't Import It
(
    var: <before>
    m: a-module: module [
    ][
        var: <inside>
    ]
    did all [
        var = <before>
        m.var = <inside>

        elide var: <outside>
        var = <outside>
        m.var = <inside>
    ]
)

; Import a Module without an Exported Variable
(
    var: <before>
    m: import mm: module [
    ][
        var: <inside>
    ]
    did all [
        m = mm
        var = <before>
        m.var = <inside>

        elide var: <outside>
        var = <outside>
        m.var = <inside>
    ]
)

; Import a Module with an Exported Variable
(
    var: <before>
    m: import mm: module [
        exports: [var]
    ][
        var: <inside>
    ]
    did all [
        m = mm
        m.var = <inside>
        var = <inside>  ; imported

        ; Overwriting m.var will not have an effect on our copy of the
        ; variable.
        ;
        elide m.var: <overwritten>
        m.var = <overwritten>
        var = <inside>

        ; The variables are fully disconnected post-import.
        ;
        elide var: <outside>
        var = <outside>
        m.var = <overwritten>
    ]
)

; Copy a Module
(
    m1: module [] [a: 10 b: 20]
    m2: copy m1
    did all [
        m1.a = m2.a
        m1.b = m2.b
        m1 = m2
    ]
)

; Import a module to a second module without contaminating the first
(
    var: <outside>

    m1: module [Exports: [var]] [var: <1>]
    m2: module [Exports: [fetch]] compose <m2compose> [
        var: <2>
        import (<m2compose> m1)
        fetch: does [var]
    ]
    did all [
        var = <outside>
        <1> = m2.fetch
    ]
)

; Overwrite a lib definition but make a helper that runs code in lib
(
    do-before: :do
    import m: module [Exports: [test]] [
        do: func [source] [throw <override>]

        emulate: func [source [block!] <local> rebound] [
            this: attach of 'any-variable-in-this-module
            assert ['do = first source]
            assert [this = binding of first source]

            ; BINDING OF doesn't account for virtual binding.  If the answer
            ; is too convoluted to use reasonably, it should probably say
            ; something like ~virtual~ back.
            ;
            rebound: lib.in lib source
            ; assert [this <> binding of first source]
            ; assert [lib = binding of first source]

            lib.do rebound
        ]

        test: does [emulate [do "1 + 2"]]
    ]
    did all [
        3 = test
        <override> = catch [m.do "1 + 2"]
        :do = :do-before
    ]
)
