REBOL []

name: 'Clipboard
source: %clipboard/mod-clipboard.c
includes: [
    %prep/extensions/clipboard ;for %tmp-ext-clipboard-init.inc
]
libraries: switch platform-config/os-base [
    'Windows [
        [%user32]
    ]
] else [null]  ; can't use null fallout in bootstrap

options: []
