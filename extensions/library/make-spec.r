REBOL []

name: 'Library
source: %library/mod-library.c
includes: [
    %prep/extensions/library
]

depends: compose [
    (switch system-config/os-base [
        'Windows [
            spread [
                [%library/library-windows.c]
            ]
        ]
    ] else [
        spread [
            [%library/library-posix.c]
        ]
    ])
]
