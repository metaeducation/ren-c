Rebol [
    name: Clipboard
    notes: "See %extensions/README.md for the format and fields of this file"
]

sources: %mod-clipboard.c

libraries: switch platform-config.os-base [
    'Windows [%user32]
] else [
    fail [
        "Clipboard extension only for Windows at this time:" newline
        "https://github.com/metaeducation/rebol-issues/issues/2029"
    ]
]
