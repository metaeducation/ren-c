REBOL [
    File: %android-cross-compiled.r
    Inherits: %android-common.r
]


toolset: compose [
    gcc (tool-for-host <compiler>)  ; detect /host as current OS
    ld  (tool-for-host <linker>)    ; same
]


cflags: reduce [
    sysroot-for-compile
    unspaced ["-D__ANDROID_API__=" android-api-level]
]


ldflags: reduce [
    sysroot-for-link
]
