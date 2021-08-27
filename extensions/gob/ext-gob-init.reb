REBOL [
    Title: "GOB! Extension"
    Name: Gob
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}

    Notes: {
        See %extensions/gob/README.md
    }
]

sys/make-scheme [
    title: "GUI Events"
    name: 'event
    actor: system/modules/Event/get-event-actor-handle
    awake: func [event] [
        print ["Default GUI event/awake:" event/type]
        true
    ]
]
