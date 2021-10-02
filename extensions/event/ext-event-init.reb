REBOL [
    Title: "EVENT! Extension"
    Name: Event
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Description: {
        The EVENT! machinery is being replaced with libuv.
    }

    Notes: {
        See %extensions/event/README.md
    }
]

; WAIT* expects block to be pre-reduced, to ease stackless implementation
;
export wait: adapt :wait* [if block? :value [value: reduce value]]
