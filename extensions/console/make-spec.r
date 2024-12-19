REBOL []

name: 'Console
source: [
    %console/mod-console.c
]

includes: [
    %prep/extensions/console  ; for %tmp-mod-console.h
]

libraries: switch platform-config.os-base [
    'Windows [
        ;
        ; Note: shell32 actually needed for CommandLineToArgvW(), which makes
        ; it more a dependency of "main" than the console module.  However,
        ; main doesn't have its own linking specification like extensions do.
        ; So put this here for now.
        ;
        ; Similarly, user32 is needed for GetWindowLong(), also more a
        ; dependency of main...
        ;
        [%shell32 %user32]
    ]
]

use-librebol: 'yes
