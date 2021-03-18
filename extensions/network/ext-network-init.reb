REBOL [
    Title: "TCP/UDP Networking"
    Name: Network
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

register-network-device

sys/make-scheme [
    title: "TCP Networking"
    name: 'tcp
    actor: get-tcp-actor-handle
    spec: system/standard/port-spec-net
    info: system/standard/net-info  ; !!! comment here said "for C enums"

    awake: func [e [event!]] [  ; Default event handler
        ;
        ; !!! This default handler had a PRINT in it in R3-Alpha.  This means
        ; if you ever returned `false` for not handling an event, it would
        ; print.  Presumably this was intended to be a more conditional
        ; network logging abstraction, that just hadn't been implemented?
        ;
        print ['TCP-event e/type]

        ; You cannot put a TRAP around an asynchronous WRITE or READ and catch
        ; the error (or at least errors related to the connection being
        ; broken during the write, etc.).  That's because network transport
        ; is happening in the event loop after the trap.  So errors are sent
        ; as events to allow them to be hooked for special behavior.  But
        ; if no special behavior is provided, it just fails.
        ;
        if e/type = 'error [
            fail e/port/error
        ]

        true  ; Ignore all other events, saying they were handled.
    ]
]

sys/make-scheme [
    title: "UDP Networking"
    name: 'udp
    actor: get-udp-actor-handle
    spec: system/standard/port-spec-net
    info: system/standard/net-info  ; !!! comment here said "for C enums"

    awake: func [e [event!]] [  ; See notes above on TCP port default handler
        print ['UDP-event e/type]
        if e/type = 'error [
            fail e/port/error
        ]
        true
    ]
]
