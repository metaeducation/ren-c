REBOL []

name: 'Event
source: %event/mod-event.c
includes: [%prep/extensions/event]

depends: compose [
    %event/t-event.c
    %event/p-event.c

    (switch system-config/os-base [
        'Windows [
            [%event/event-windows.c]
        ]
    ] else [
        [%event/event-posix.c]
    ])
]

libraries: try switch system-config/os-base [
    'Windows [
        ;
        ; Needed for SetTimer(), GetMessage(), etc.
        ;
        [%user32]
    ]
]
