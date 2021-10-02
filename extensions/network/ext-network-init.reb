REBOL [
    Title: "TCP/UDP Networking"
    Name: Network
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys/make-scheme [
    title: "TCP Networking"
    name: 'tcp
    actor: get-tcp-actor-handle
    spec: system/standard/port-spec-net
    info: system/standard/net-info  ; !!! comment here said "for C enums"
]

; NOTE: UDP Networking has not been rewritten for the libuv transition.  It
; is kept here as a reminder of that.
;
sys/make-scheme [
    title: "UDP Networking"
    name: 'udp
    actor: get-udp-actor-handle
    spec: system/standard/port-spec-net
    info: system/standard/net-info  ; !!! comment here said "for C enums"
]
