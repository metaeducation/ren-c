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
    port: construct system/standard/port []
    port/spec: construct any [scheme/spec system/standard/port-spec-head] spec
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

*parse-url: has [
    digit:       make bitset! "0123456789"
    digits:      [1 5 digit]
    alpha-num:   make bitset! [#"a" - #"z" #"A" - #"Z" #"0" - #"9"]
    scheme-char: insert copy alpha-num "+-."
    path-char:   insert copy alpha-num "!/=+-_.;:&$@%*',~?| []()^"" ; !!! note: space allowed
    user-char:   insert copy alpha-num "=+-_.;&$@%*,'#|"
    pass-char:   complement make bitset! "^/ ^-@"
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
                    scheme: (uneval to word! to text! s1)
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
                copy s1 any user-char
                opt [
                    ":" copy s2 digits (
                        append out compose [
                            port-id: (to-integer/unsigned s2)
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

                    if not empty? trim s1 [
                        attempt [s1: to tuple! s1]
                        emit host s1
                    ]
                )
            ]
        ]

        ; optional path
        opt [copy s1 some path-char (emit path s1)]

        ; optional bookmark
        opt ["#" copy s1 some path-char (emit tag s1)]

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
    "Make a scheme from a specification and add it to the system."
    def [block!]
        "Scheme specification"
    /with
    base-name [word!]
        "Scheme name to use as base"
][
    with: either with [get in system/schemes base-name][system/standard/scheme]
    if not with [cause-error 'access 'no-scheme base-name]

    scheme: construct with def
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

    make-scheme [
        title: "System Port"
        name: 'system
        actor: get-event-actor-handle
        awake: func [
            sport "System port (State block holds events)"
            ports "Port list (Copy of block passed to WAIT)"
            /only
            <local> event event-list n-event port waked
        ][
            waked: sport/data ; The wake list (pending awakes)

            if only and [not block? ports] [
                return blank ; short cut for a pause
            ]

            ; Process all events (even if no awake ports).
            n-event: 0
            event-list: sport/state
            while [not empty? event-list][
                if n-event > 8 [break] ; Do only 8 events at a time (to prevent polling lockout).
                event: first event-list
                port: event/port
                either any [
                    not only
                    find ports port
                ][
                    remove event-list ;avoid event overflow caused by wake-up recursively calling into wait
                    if wake-up port event [
                        ; Add port to wake list:
                        ;print ["==System-waked:" port/spec/ref]
                        if not find waked port [append waked port]
                    ]
                    n-event: n-event + 1
                ][
                    event-list: next event-list
                ]
            ]

            ; No wake ports (just a timer), return now.
            if not block? ports [return blank]

            ; Are any of the requested ports awake?
            for-each port ports [
                find waked port then [return true]
            ]

            false ; keep waiting
        ]
        init: func [port] [
            ;;print ["Init" title]
            port/data: copy [] ; The port wake list
            return
        ]
    ]

    make-scheme [
        title: "Console Access"
        name: 'console
        actor: get-console-actor-handle
    ]

    make-scheme [
        title: "File Access"
        name: 'file
        actor: get-file-actor-handle
        info: system/standard/file-info ; for C enums
        init: func [port <local> path] [
            if url? port/spec/ref [
                parse port/spec/ref [thru #":" 0 2 slash path:]
                append port/spec compose [path: (to file! path)]
            ]
            return
        ]
    ]

    make-scheme/with [
        title: "File Directory Access"
        name: 'dir
        actor: get-dir-actor-handle
    ] 'file

    make-scheme [
        title: "GUI Events"
        name: 'event
        actor: get-event-actor-handle
        awake: func [event] [
            print ["Default GUI event/awake:" event/type]
            true
        ]
    ]

    make-scheme [
        title: "DNS Lookup"
        name: 'dns
        actor: get-dns-actor-handle
        spec: system/standard/port-spec-net
        awake: func [event] [print event/type true]
    ]

    make-scheme [
        title: "TCP Networking"
        name: 'tcp
        actor: get-tcp-actor-handle
        spec: system/standard/port-spec-net
        info: system/standard/net-info ; for C enums
        awake: func [event] [print ['TCP-event event/type] true]
    ]

    make-scheme [
        title: "UDP Networking"
        name: 'udp
        actor: get-udp-actor-handle
        spec: system/standard/port-spec-net
        info: system/standard/net-info ; for C enums
        awake: func [event] [print ['UDP-event event/type] true]
    ]

    if 4 == fourth system/version [
        make-scheme [
            title: "Signal"
            name: 'signal
            actor: get-signal-actor-handle
            spec: system/standard/port-spec-signal
        ]
    ]

    make-scheme [
        title: "Serial Port"
        name: 'serial
        actor: get-serial-actor-handle
        spec: system/standard/port-spec-serial
        init: func [port <local> path speed] [
            if url? port/spec/ref [
                parse port/spec/ref [
                    thru #":" 0 2 slash
                    copy path [to slash | end] skip
                    copy speed to end
                ]
                attempt [port/spec/speed: to-integer/unsigned speed]
                port/spec/path: to file! path
            ]
            return
        ]
    ]

    system/ports/system:   open [scheme: 'system]
    system/ports/input:    open [scheme: 'console]

    init-schemes: 'done ; only once
]
