REBOL []

name: 'Process
source: %process/mod-process.c

; The implementation CALL is pretty much entirely different on Windows.  The
; only sensible abstraction is CALL itself.  We don't want to repeat the
; spec for call, so that is in %mod-process.c, but the implementations are
; in separate files, as C functions parameterized by the frame.
;
depends: switch platform-config/os-base [
    'Windows [
        [%process/call-windows.c]
    ]
] else [
    [%process/call-posix.c]
]

includes: copy [
    %prep/extensions/process ;for %tmp-extensions-process-init.inc
]

requires: 'Filesystem  ; for FILE-TO-LOCAL in CALL
