Rebol [
    file: %mingw-x64-c++r

    inherits: %default-config.r
]

os-id: 0.3.40

standard: default ['c++]

compiler: 'gcc
compiler-path: %x86_64-w64-mingw32-g++

; When using <stdint.h> with some older compilers, these definitions are
; needed, otherwise you won't get INT32_MAX or UINT64_C() etc.
;
; https://sourceware.org/bugzilla/show_bug.cgi?id=15366
;
cflags: [
    "-D__STDC_LIMIT_MACROS"
    "-D__STDC_CONSTANT_MACROS"
]
