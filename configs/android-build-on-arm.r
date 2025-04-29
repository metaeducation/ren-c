Rebol [
    file: %android-arm-native.r
    inherits: %android-common.r
]

compiler: 'gcc
compiler-path: tool-for-host:host <compiler> 'linux-arm

comment [  ; !!! new philosophy is use compiler front end to link, always
    linker: 'ld
    linker-path: tool-for-host:host <linker> 'linux-arm
]

cflags: reduce [
    sysroot-for-compile
]

ldflags: reduce [
    sysroot-for-link
]
