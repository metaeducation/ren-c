REBOL []

name: 'Time
source: %time/mod-time.c
includes: [
    %prep/extensions/time
]

depends: compose [  ; Note: must work in COMPOSE and COMPOSE2 !
    ((switch system-config/os-base [
        'Windows [
            [
                [%time/time-windows.c]
            ]
        ]
    ] else [
        [
            [%time/time-posix.c]
        ]
    ]))
]
