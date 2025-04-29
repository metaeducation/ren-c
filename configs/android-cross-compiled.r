Rebol [
    file: %android-cross-compiled.r
    inherits: %android-common.r
]

compiler: 'gcc
compiler-path: tool-for-host <compiler>  ; detect :host as current OS

comment [  ; !!! new philosophy is use compiler front end to link, always
    linker: 'ld
    linker-path: tool-for-host <linker>  ; detect :host as current OS
]

cflags: reduce [
    sysroot-for-compile
    unspaced ["-D__ANDROID_API__=" android-api-level]
]

ldflags: reduce [
    sysroot-for-link
]
