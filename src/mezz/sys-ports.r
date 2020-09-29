REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Port and Scheme Functions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Context: sys
    Note: {
        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!
    }
]

make-port*: function [
    "SYS: Called by system on MAKE of PORT! port from a scheme."

    spec [file! url! block! object! word! port!]
        "port specification"
][
    ; The first job is to identify the scheme specified:

    switch type of spec [
        file! [
            name: pick [dir file] dir? spec
            spec: compose [ref: (spec)]
        ]
        url! [
            spec: append decode-url spec compose [ref: (spec)]
            name: try select spec to set-word! 'scheme
        ]
        block! [
            name: try select spec to set-word! 'scheme
        ]
        object! [
            name: try get in spec 'scheme
        ]
        word! [
            name: spec
            spec: []
        ]
        port! [
            name: port/scheme/name
            spec: port/spec
        ]

        fail
    ]

    ; Get the scheme definition:
    name: dequote name
    all [
        word? name
        scheme: get try in system/schemes name
    ] else [
        cause-error 'access 'no-scheme name
    ]

    ; Create the port with the correct scheme spec:
    port: make system/standard/port []
    port/spec: make any [
        scheme/spec system/standard/port-spec-head
    ] spec
    port/spec/scheme: name
    port/scheme: scheme

    ; Defaults:
    port/actor: try get in scheme 'actor ; avoid evaluation
    port/awake: try any [
        get try in port/spec 'awake
        get 'scheme/awake
    ]
    port/spec/ref: default [spec]
    port/spec/title: default [scheme/title]
    port: to port! port

    ; Call the scheme-specific port init. Note that if the
    ; scheme has not yet been initialized, it can be done
    ; at this time.
    if in scheme 'init [scheme/init port]
    port
]

*parse-url: make object! [
    digit:       make bitset! "0123456789"
    digits:      [1 5 digit]
    alpha-num:   make bitset! [#"a" - #"z" #"A" - #"Z" #"0" - #"9"]
    scheme-char: insert copy alpha-num "+-."
    path-char:   complement make bitset! "#"
    user-char:   complement make bitset! ":@"
    host-char:   complement make bitset! ":/?"
    s1: s2: _ ; in R3, input datatype is preserved - these are now URL strings
    out: []
    emit: func ['w v] [
        append out reduce [
            to set-word! w (either :v [to text! :v] [_])
        ]
    ]

    rules: [
        ; Scheme://user-host-part
        [
            ; scheme name: [//]
            copy s1 some scheme-char ":" opt "//" ( ; "//" is optional ("URN")
                append out compose [
                    scheme: '(as word! s1)
                ]
            )

            ; optional user [:pass]
            opt [
                copy s1 some user-char
                opt [":" copy s2 to "@" (emit pass s2)]
                "@" (emit user s1)
            ]

            ; optional host [:port]
            opt [
                copy s1 any host-char
                opt [
                    ":" copy s2 digits (
                        append out compose [
                            port-id: (to integer! s2)
                        ]
                    )
                ] (
                    ; Note: This code has historically attempted to convert
                    ; the host name into a TUPLE!, and if it succeeded it
                    ; considers this to represent an IP address lookup vs.
                    ; a DNS lookup.  A basis for believing this will work can
                    ; come from RFC-1738:
                    ;
                    ; "The rightmost domain label will never start with a
                    ;  digit, though, which syntactically distinguishes all
                    ;  domain names from the IP addresses."
                    ;
                    ; This suggests that as long as a TUPLE! conversion will
                    ; never allow non-numeric characters it can work, though
                    ; giving a confusing response to looking up "1" to come
                    ; back and say "1.0.0 cannot be found", because that is
                    ; the result of `make tuple! "1"`.
                    ;
                    ; !!! This code was also broken in R3-Alpha, because the
                    ; captured content in PARSE of a URL! was a URL! and not
                    ; a STRING!, and so the attempt to convert `s1` to TUPLE!
                    ; would always fail.  Ren-C permits this conversion.
                    ;
                    ; !!! Now that things are updated, should it *always* be
                    ; a TUPLE!, instead of only when it's an IP address?

                    if not empty? trim s1 [
                        use [tup] [
                            tup: to tuple! s1
                            if integer? last tup [
                                s1: tup
                            ]
                        ]
                        emit host s1
                    ]
                )
            ]
        ]

        ; optional path
        opt [copy s1 some path-char (emit path s1)]

        ; optional bookmark
        opt ["#" copy s1 to end (emit tag s1)]

        end
    ]

    decode-url: func ["Decode a URL according to rules of sys/*parse-url." url] [
        ; This function is bound in the context of sys/*parse-url
        out: make block! 8
        parse url rules
        out
    ]
]

decode-url: _ ; used by sys funcs, defined above, set below

;-- Native Schemes -----------------------------------------------------------

make-scheme: function [
    {Make a scheme from a specification and add it to the system}

    def "Scheme specification"
        [block!]
    /with "Scheme name to use as base"
        [word!]
][
    with: either with [get in system/schemes with][system/standard/scheme]
    if not with [cause-error 'access 'no-scheme with]

    scheme: make with def
    if not scheme/name [cause-error 'access 'no-scheme-name scheme]

    ; If actor is block build a non-contextual actor object:
    if block? :scheme/actor [
        actor: make object! (length of scheme/actor) / 4
        for-each [name func* args body] scheme/actor [
            ; !!! Comment here said "Maybe PARSE is better here", though
            ; knowing would depend on understanding precisely what the goal
            ; is in only allowing FUNC vs. alternative function generators.
            assert [
                set-word? name
                func* = 'func
                block? args
                block? body
            ]
            append actor reduce [
                name (func args body) ; add action! to object! w/name
            ]
        ]
        scheme/actor: actor
    ]

    match [object! handle!] :scheme/actor else [
        fail ["Scheme actor" :scheme/name "can't be" type of :scheme/actor]
    ]

    append system/schemes reduce [scheme/name scheme]
]

init-schemes: func [
    "INIT: Init system native schemes and ports."
][
    sys/decode-url: lib/decode-url: :sys/*parse-url/decode-url

    system/schemes: make object! 10

    init-schemes: 'done ; only once
]
