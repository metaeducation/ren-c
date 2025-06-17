Rebol [
    name: Console
    notes: "See %extensions/README.md for the format and fields of this file"
]

use-librebol: 'yes

includes: []

sources: [mod-console.c]

; Note: shell32 actually needed for CommandLineToArgvW(), which makes it more
; a dependency of "main" than the console module.  However, main doesn't have
; its own linking specification like extensions do.  So put this here for now.
;
; (Similarly, user32 is needed for GetWindowLong(), also more a dependency
; of main...)
;
libraries: switch platform-config.os-base [
    'Windows [
        [%shell32 %user32]
    ]
]
