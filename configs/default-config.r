REBOL []
os-id: null

; possible values (words):
; Execution: Build the target directly without generating a Makefile
; makefile: Generate a makefile for GNU make
; nmake: Generate an NMake file for CL
target: 'execution

extensions: make map! [
    ; NAME VALUE
    ; VALUE: one of
    ; + builtin
    ; - disabled
    ; * dynamic
    ; [modules] dynamic with selected modules

    ; FFI and ODBC have dependencies outside of what's available on a stock
    ; standard C compiler with POSIX or Win32.  Disable these extensions by
    ; default.  (Review the general policy for default inclusions.)
    ; Clipboard is only implemented in Windows at the moment.

    Clipboard -
    Console +
    Crypt +
    Debugger +
    DNS +
    Filesystem +
    JavaScript -
    Locale +
    Network +
    ODBC -
    Process +
    TCC -
    Time +
    UUID +
    UTF +
    View +
]

rebol-tool: null ; fallback value if system/options/boot fails

; possible combination:
; [gcc _ ld _]
; [cl _ link _]
toolset: [
    ;name executable-file-path (_ being default)
    gcc _
    ld _
    strip _
]

;one of "no", "'asserts", "'symbols" or "'sanitize"
debug: no

; one of 'no', 1, 2 or 4
;
optimize: 2

; one of [c gnu89 gnu99 c99 c11 c++ c++98 c++0x c++11 c++14 c++17]
;
standard: 'c

rigorous: no

static: no
pkg-config: get-env "PKGCONFIG"  ; path to pkg-config, or default

odbc-requires-ltdl: no

with-tcc: no

; Console API for windows does not exist before vista.
;
pre-vista: no


git-commit: null

includes: null
definitions: null
cflags: null
libraries: null
ldflags: null

main: null

top: null

config: null
