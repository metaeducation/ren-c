Rebol []
os-id: null

; possible values (words):
; Execution: Build the target directly without generating a Makefile
; makefile: Generate a makefile for GNU make
; nmake: Generate an NMake file for CL
target: 'execution

extensions: to map! [
    ; NAME VALUE
    ; VALUE: one of
    ; + builtin
    ; - disabled
    ; * dynamic
    ; [modules] dynamic with selected modules

    ; !!! Review default clipboard inclusion

    Console +
    Crypt +
    Debugger +
    DNS +
    Environment +
    Filesystem +
    Network +
    Process +
    Stdio +
    Time +
    UUID +
    View +
]

rebol-tool: null  ; fallback value if system.options.boot unavailable

compiler: null  ; e.g. g++ clang etc. default to cc if not set
compiler-path: null  ; will use compiler name as default if not overridden

debug: 'none  ; [none asserts symbols sanitize]

; one of 'no', 1, 2 or 4
;
optimize: 2

; one of [c gnu89 gnu99 c99 c11 c++ c++98 c++0x c++11 c++14 c++17]
;
standard: 'c

rigorous: 'no

static: 'no
pkg-config: get-env "PKGCONFIG"  ; path to pkg-config, or default

odbc-requires-ltdl: 'no

with-tcc: 'no

; Console API for windows does not exist before vista.
;
pre-vista: 'no


git-commit: null

includes: null
definitions: null
cflags: null
libraries: null
ldflags: null

main: null

top: null

config: null
