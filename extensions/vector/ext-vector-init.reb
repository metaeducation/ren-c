REBOL [
    Title: "Vector Extension"
    Name: Vector
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}

    Notes: {
        See %extensions/vector/README.md
    }
]

; !!! Should call UNREGISTER-VECTOR-HOOKS at some point (module finalizer?)
;
register-vector-hooks

export []
