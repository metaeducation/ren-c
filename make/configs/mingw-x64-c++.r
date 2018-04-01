REBOL []

os-id: 0.3.40
standard: 'c++
toolset: [
    gcc %x86_64-w64-mingw32-g++
    ld %x86_64-w64-mingw32-g++; linking is done via calling g++, not ld
    strip %x86_64-w64-mingw32-strip
]

; When using <stdint.h> with some older compilers, these definitions are
; needed, otherwise you won't get INT32_MAX or UINT64_C() etc.
;
; https://sourceware.org/bugzilla/show_bug.cgi?id=15366
;
cflags: [
    "-D__STDC_LIMIT_MACROS"
    "-D__STDC_CONSTANT_MACROS"
]
