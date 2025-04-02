REBOL [
    Name: Stdio
    Notes: "See %extensions/README.md for the format and fields of this file"
]

includes: []

sources: [
    %mod-stdio.c
    %p-stdio.c
]

depends: compose [
    (switch platform-config.os-base [
        'Windows [
            spread [
                [%stdio-windows.c]
                [%readline-windows.c]
            ]
        ]
    ] else [
        spread [
            [%stdio-posix.c]
            [%readline-posix.c]
        ]
    ])
]
