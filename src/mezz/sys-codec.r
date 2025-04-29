Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Sys: Encoder and Decoder"
    rights: --{
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }--
    license: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    context: sys
    notes: --{
        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!
    }--
]


; !!! There should also be an unregister-codec
;
register-codec: func [
    return: [object!]
    name "Descriptive name of the codec"
        [word!]
    suffixes "File extension or block of file extensions the codec processes"
        [file! block!]
    identify? [~null~ <unrun> frame!]
    decode [~null~ <unrun> frame!]
    encode [~null~ <unrun> frame!]
    <local> codec
][
    if not block? suffixes [suffixes: reduce [suffixes]]

    codec: make object! compose [
        name: '(name)

        ; !!! There was a "type:" field here before, which was always set to
        ; IMAGE!.  Should the argument types of the encode function be cached
        ; here, or be another parameter, or...?

        suffixes: (^ suffixes)
        identify?: (^ identify?)
        decode: (^ decode)
        encode: (^ encode)
    ]

    set (extend system.codecs name) codec

    ; Media-types block format: [.abc .def type ...]
    ; !!! Should be a map, with blocks of codecs on collisions
    ;
    append append system.options.file-types suffixes (bind system.codecs name)

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


decode: func [
    "Decodes a series of bytes into the related datatype (e.g. image!)"

    type "Media type (jpeg, png, etc.)"
        [word! block!]
    data "The data to decode"
        [blob!]
][
    let options: either block? type [
        type: compose2 inside type '@() type
        next type
        elide type: first type
    ][
        '[]
    ]
    all [
        let cod: select system.codecs type
        (data: run cod.decode data options)
    ] else [
        cause-error 'access 'no-codec type
    ]
    return data
]


encode: func [
    "Encodes a datatype (e.g. image!) into a series of bytes"

    return: [blob!]
    type "Media type (jpeg, png, etc.)"
        [word! block!]
    data [element?]
][
    let options: either block? type [
        type: compose2 inside type '@() type
        next type
        elide type: first type
    ][
        '[]
    ]
    all [
        let cod: select system.codecs type
        (data: run cod.encode data options)
    ] else [
        cause-error 'access 'no-codec type
    ]
    return data
]


encoding-of: func [
    "Returns the media codec name for given binary data. (identify)"

    return: [~null~ word!]
    data [blob!]
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
