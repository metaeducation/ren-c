Rebol []

os-id: 0.13.2

tool-prefix: to-file opt get-env "ANDROID_NDK"

compiler: 'gcc
compiler-path: tool-prefix/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc

ldflags: cflags: reduce [
    unspaced ["--sysroot=" tool-prefix/platforms/android-19/arch-arm]
]
