Rebol [
    name: Environment
    notes: "See %extensions/README.md for the format and fields of this file"

    extended-types: [environment!]
]

use-librebol: 'no  ; currently extending datatypes/generics needs %sys-core.h

sources: %mod-environment.c

depends: switch platform-config.os-base [
    'Windows [
        [%env-windows.c]
    ]
] else [
    [%env-posix.c]
]
