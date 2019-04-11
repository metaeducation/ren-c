REBOL [
    Title: "Image Extension"
    Name: Image
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Notes: {
        See %extensions/image/README.md
    }
]

; !!! Should call UNREGISTER-IMAGE-HOOKS at some point (module finalizer?)
;
register-image-hooks [
    complement: generic [
        value [image!]
    ]
]


sys/export []  ; current hacky mechanism is to put any exports here
