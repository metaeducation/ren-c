REBOL [
    Title: "POSIX Signal Extension"
    Name: Signal
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]


sys/make-scheme [
    title: "Signal"
    name: 'signal
    actor: get-signal-actor-handle
    spec: system/standard/port-spec-signal
]
