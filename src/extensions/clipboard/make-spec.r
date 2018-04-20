REBOL []

name: 'Clipboard
source: %clipboard/ext-clipboard.c
init: %clipboard/ext-clipboard-init.reb
modules: [
    [
        name: 'Clipboard
        source: [
            %clipboard/mod-clipboard.c
        ]
        includes: [
            %prep/extensions/clipboard ;for %tmp-ext-clipboard-init.inc
        ]
        libraries: _
    ]
]

options: []
