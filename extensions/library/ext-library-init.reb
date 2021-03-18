REBOL [
    Title: "Library/DLL Extension"
    Name: Library
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

; !!! Should call UNREGISTER-LIBRARY-HOOKS at some point (module finalizer?)
;
register-library-hooks [  ; !!! Parameter is documentation only, see notes
    close: generic [
        return: [<opt> any-value!]
        port [library!]
    ]
]
