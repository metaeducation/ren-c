REBOL []

name: 'Stdio
source: %stdio/mod-stdio.c
includes: [
    %prep/extensions/stdio
]

depends: compose2 [
    %stdio/p-stdio.c

    ((switch system-config/os-base [
        'Windows [
            [
                [%stdio/stdio-windows.c]
                [%stdio/readline-windows.c]
            ]
        ]
    ] else [
        [
            [%stdio/stdio-posix.c]
            [%stdio/readline-posix.c]
        ]
    ]))
]
