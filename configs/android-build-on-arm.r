REBOL [
    File: %android-arm-native.r
    Inherits: %android-common.r
]


toolset: compose [
    gcc (tool-for-host/host <compiler> 'linux-arm)
    ld  (tool-for-host/host <linker> 'linux-arm)
]


cflags: reduce [
    sysroot-for-compile
]


ldflags: reduce [
    sysroot-for-link
]
