REBOL [
    Name: Process
    Notes: "See %extensions/README.md for the format and fields of this file"
]

use-librebol: 'no

requires: 'Filesystem  ; for FILE-TO-LOCAL in CALL

sources: %mod-process.c

; The implementation CALL is pretty much entirely different on Windows.  The
; only sensible abstraction is CALL itself.  We don't want to repeat the
; spec for call, so that is in %mod-process.c, but the implementations are
; in separate files, as C functions parameterized by the frame.
;
depends: switch platform-config.os-base [
    'Windows [
        [%call-windows.c]
    ]
] else [
    [%call-posix.c]
]
