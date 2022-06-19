REBOL []

name: 'Library
source: %library/mod-library.c
includes: [
    %prep/extensions/library
]

depends: compose [  ; Note: must work in COMPOSE and COMPOSE2 !
    ((switch system-config/os-base [
        'Windows [
            [
                [%library/library-windows.c]
            ]
        ]
    ] else [
        [
            [%library/library-posix.c]
        ]
    ]))
]
