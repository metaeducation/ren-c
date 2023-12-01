REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Encoder and Decoder"
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


; This function is called by the C function Register_Codec(), but can
; also be called by user code.
;
; !!! There should also be an unregister-codec*
;
register-codec*: func [
    return: [object!]
    name [word!]
        {Descriptive name of the codec.}
    suffixes [file! block!]
        {File extension or block of file extensions the codec processes}
    identify? [<opt> <unrun> frame!]
    decode [<opt> <unrun> frame!]
    encode [<opt> <unrun> frame!]
    <local> codec
][
    if not block? suffixes [suffixes: reduce [suffixes]]

    codec: make object! compose [
        name: '(name)

        ; !!! There was a "type:" field here before, which was always set to
        ; IMAGE!.  Should the argument types of the encode function be cached
        ; here, or be another parameter, or...?

        suffixes: '(suffixes)
        identify?: '(identify?)
        decode: '(decode)
        encode: '(encode)
    ]

    append system.codecs spread reduce [(to set-word! name) codec]

    ; Media-types block format: [.abc .def type ...]
    ; !!! Should be a map, with blocks of codecs on collisions
    ;
    append append system.options.file-types suffixes (bind name system.codecs)

    return codec
]


; Special import case for extensions:
append system.options.file-types spread switch fourth system.version [
    3 [
        [%.rx %.dll extension] ; Windows
    ]
    2 [
        [%.rx %.dylib %.so extension] ; OS X
    ]
    4
    7 [
        [%.rx %.so extension] ; Other Posix
    ]
] else [
    [%.rx extension]
]


decode: function [
    {Decodes a series of bytes into the related datatype (e.g. image!).}

    type [word!]
        {Media type (jpeg, png, etc.)}
    data [binary!]
        {The data to decode}
][
    all [
        cod: select system.codecs type
        (data: run cod.decode data)
    ] else [
        cause-error 'access 'no-codec type
    ]
    return data
]


encode: function [
    {Encodes a datatype (e.g. image!) into a series of bytes.}

    return: [binary!]
    type "Media type (jpeg, png, etc.)"
        [word!]
    data [element?]
    /options "Encoding options"
        [block!]  ; !!! Not currently used
][
    all [
        cod: select system.codecs type
        (data: run cod.encode data)
    ] else [
        cause-error 'access 'no-codec type
    ]
    return data
]


encoding-of: function [
    "Returns the media codec name for given binary data. (identify)"

    return: [<opt> word!]
    data [binary!]
][
    for-each [name codec] system.codecs [
        if all [
            (run codec.identify? data)
        ][
            return name
        ]
    ]
    return null
]
