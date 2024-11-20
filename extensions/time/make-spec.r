REBOL []

name: 'Time
source: %time/mod-time.c
includes: [
    %prep/extensions/time
]

depends: compose1 [
    (switch platform-config.os-base [
        'Windows [
            spread [
                [%time/time-windows.c]
            ]
        ]
    ] else [
        spread [
            [%time/time-posix.c]
        ]
    ])
]
