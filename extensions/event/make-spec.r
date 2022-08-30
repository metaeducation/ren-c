REBOL []

name: 'Event
source: %event/mod-event.c
includes: [%prep/extensions/event]

depends: compose [
    %event/t-event.c

    (switch system-config/os-base [
        'Windows [
            spread [
                [%event/event-windows.c]
            ]
        ]
    ] else [
        spread [
            [%event/event-posix.c]
        ]
    ])
]

libraries: maybe switch system-config/os-base [
    'Windows [
        ;
        ; Needed for SetTimer(), GetMessage(), etc.
        ;
        [%user32]
    ]
]
