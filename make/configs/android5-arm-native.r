REBOL []

os-id: 0.13.2

tool-prefix: to-file get-env "ANDROID_NDK"
gcc-path: tool-prefix/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-arm/bin/arm-linux-androideabi-gcc

toolset: compose [
    gcc (gcc-path)
    ld  (gcc-path)
]

ldflags: cflags: reduce [
    unspaced ["--sysroot=" tool-prefix/platforms/android-19/arch-arm]
]
