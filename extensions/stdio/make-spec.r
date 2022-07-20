REBOL []

name: 'Stdio
source: %stdio/mod-stdio.c
includes: [
    %prep/extensions/stdio
]

depends: compose [
    %stdio/p-stdio.c

    (switch system-config/os-base [
        'Windows [
            spread [
                [%stdio/stdio-windows.c]
                [%stdio/readline-windows.c]
            ]
        ]
    ] else [
        spread [
            [%stdio/stdio-posix.c]
            [%stdio/readline-posix.c]
        ]
    ])
]
