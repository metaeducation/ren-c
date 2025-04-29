Rebol [
    name: Time
    notes: "See %extensions/README.md for the format and fields of this file"
]

use-librebol: 'no

sources: %mod-time.c

depends: compose [
    (switch platform-config.os-base [
        'Windows [
            %time-windows.c
        ]
    ] else [
        %time-posix.c
    ])
]
