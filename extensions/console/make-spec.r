REBOL []

name: 'Console
source: %console/mod-console.c

includes: [
    %prep/extensions/console  ; for %tmp-mod-console.h
]

libraries: try switch system-config/os-base [
    'Windows [
        ;
        ; Note: This is actually needed for CommandLineToArgvW(), which makes
        ; it more a dependency of "main" than the console module.  However,
        ; main doesn't have its own linking specification like extensions do.
        ; So put this here for now.
        ;
        [%shell32]
    ]
]
