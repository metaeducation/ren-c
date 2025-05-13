Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Sys: Port and Scheme Functions"
    rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    context: sys
    notes: {
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

    really switch type of spec [
        file! [
            name: pick [dir file] dir? spec
            spec: append copy [ref:] spec
        ]
        url! [
            spec: append (decode-url spec) reduce [to set-word! 'ref spec]
            name: select spec to set-word! 'scheme
            if not lit-word? name [
                panic ["BLOCK! scheme name is LIT-WORD!, not:" name]
            ]
            name: unmeta name
        ]
        block! [
            name: select spec to set-word! 'scheme
            if not lit-word? name [
                panic ["BLOCK! scheme name is LIT-WORD!, not:" name]
            ]
            name: unmeta name
        ]
        object! [
            name: get in spec 'scheme
        ]
        word! [
            name: spec
            spec: []
        ]
        port! [
            name: port/scheme/name
            spec: port/spec
        ]
    ]

    ; Get the scheme definition:
    all [
        match [word!] name
        scheme: get opt in system/schemes name
    ] else [
        cause-error 'access 'no-scheme name
    ]

    ; Create the port with the correct scheme spec:
    port: make system/standard/port []
    port/spec: make any [scheme/spec system/standard/port-spec-head] spec
    port/spec/scheme: ensure word! name
    port/scheme: scheme

    ; Defaults:
    port/actor: get in scheme 'actor ; avoid evaluation
    port/awake: any [
        get opt in port/spec 'awake
        get 'scheme/awake
    ]
    port/spec/ref: default [spec]
    port/spec/title: default [scheme/title]
    port: to port! port

    ; Call the scheme-specific port init. Note that if the
    ; scheme has not yet been initialized, it can be done
    ; at this time.
    if in scheme 'init [scheme/init port]
    return port
]

*parse-url: make object! [
    digit:       make bitset! "0123456789"
    digits:      [1 5 digit]
    alpha-num:   make bitset! [#"a" - #"z" #"A" - #"Z" #"0" - #"9"]
    scheme-char: insert copy alpha-num "+-."
    path-char:   insert copy alpha-num "!/=+-_.;:&$@%*',~?| []()^"" ; !!! note: space allowed
    user-char:   insert copy alpha-num "=+-_.;&$@%*,'#|"
    pass-char:   complement make bitset! "^/ ^-@"
    s1: s2: null  ; in R3, input datatype is preserved, now URL strings
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
            s1: across some scheme-char ":" opt "//" (  ; "//" optional ("URN")
                append out compose [
                    scheme: (meta to word! s1)
                ]
            )

            ; optional user [:pass]
            opt [
                s1: across some user-char
                opt [":" s2: across to "@" (emit pass s2)]
                "@" (emit user s1)
            ]

            ; optional host [:port]
            opt [
                s1: across opt some user-char
                opt [
                    ":" s2: across digits (
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
                        sys/util/rescue [s1: to tuple! s1]
                        emit host s1
                    ]
                )
            ]
        ]

        ; optional path
        opt [s1: across some path-char (emit path s1)]

        ; optional bookmark
        opt ["#" s1: across some path-char (emit tag s1)]

        <end>
    ]

    decode-url: func [
        "Decode a URL according to rules of sys/util/*parse-url."
        return: [block!]
        url
    ][
        ; This function is bound in the context of sys/util/*parse-url
        out: make block! 8
        parse url rules
        return out
    ]
]

decode-url: ~  ; used by sys funcs, defined above, set below

;-- Native Schemes -----------------------------------------------------------

make-scheme: function [
    "Make a scheme from a specification and add it to the system."
    return: [~]
    def [block!]
        "Scheme specification"
    /with
    base-name [word!]
        "Scheme name to use as base"
][
    with: either with [get in system/schemes base-name][system/standard/scheme]
    if not with [cause-error 'access 'no-scheme base-name]

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
        panic ["Scheme actor" :scheme/name "can't be" type of :scheme/actor]
    ]

    append system/schemes reduce [scheme/name scheme]
]

init-schemes: func [
    return: [~]
    "INIT: Init system native schemes and ports."
][
    sys/util/decode-url: lib/decode-url: :sys/util/*parse-url/decode-url

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

            if only and (not block? ports) [
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
                find waked port then [return okay]
            ]

            return null ; keep waiting
        ]
        init: func [port] [
            ;;print ["Init" title]
            port/data: copy [] ; The port wake list
            return ~
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
                parse port/spec/ref [thru #":" 0 2 slash path: <here>]
                append port/spec compose [path: (to file! path)]
            ]
            return ~
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
            return okay
        ]
    ]

    make-scheme [
        title: "DNS Lookup"
        name: 'dns
        actor: get-dns-actor-handle
        spec: system/standard/port-spec-net
        awake: func [event] [
            print event/type
            return okay
        ]
    ]

    make-scheme [
        title: "TCP Networking"
        name: 'tcp
        actor: get-tcp-actor-handle
        spec: system/standard/port-spec-net
        info: system/standard/net-info ; for C enums
        awake: func [event] [
            print ['TCP-event event/type]
            return okay
        ]
    ]

    make-scheme [
        title: "UDP Networking"
        name: 'udp
        actor: get-udp-actor-handle
        spec: system/standard/port-spec-net
        info: system/standard/net-info ; for C enums
        awake: func [event] [
            print ['UDP-event event/type]
            return okay
        ]
    ]

    system/ports/system:   open [scheme: 'system]
    system/ports/input:    open [scheme: 'console]

    init-schemes: 'done ; only once
]
